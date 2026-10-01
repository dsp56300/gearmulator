#pragma once

#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "../jitCompileWorker.h"

#include "lsp_interpreter.h"
#include "lsp_jit.h"

namespace lspLib
{
	// Host interface; runs the JIT when the current program is compiled (../jitCompileWorker.h) and the
	// interpreter otherwise.
	class LSPDispatcher
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;	// left, right
		struct Snapshot
		{
			std::vector<LSPInstr> instr = std::vector<LSPInstr>(ProgramWords);
		};

		enum HostRegister : uint16_t
		{
			HostAddrLo  = 0x00,	// writing commits the staged IRAM word
			HostAddrHi  = 0x01,
			HostDataLo  = 0x02,
			HostDataMid = 0x03,	// reading returns ready status
			HostDataHi  = 0x04,
			HostConfig  = 0x06,
			HostReadLo  = 0x08,
			HostReadHi  = 0x09,
		};

		LSPDispatcher()
			: m_runtime(new LSPRuntime())
			, m_program(new LSPProgram())
			, m_jit(new LSPJIT(*m_program, *m_runtime))
			, m_compiler([this](const Snapshot& _s) { return m_jit->compile(_s.instr.data()); },
				[this] { return chips::pollHostBackend(*m_jit); })
		{
		}

		LSPDispatcher(const LSPDispatcher&) = delete;
		LSPDispatcher& operator=(const LSPDispatcher&) = delete;

		void clear()
		{
			m_program->clear();
			m_runtime->clear();
			m_canJit = false;
			m_config = 0;
			m_running = false;
			m_hostLatch = 0;
			m_hostReadAddr = 0;
		}

		void clearERAM() { m_runtime->clearEram(); }

		// ---- Host aperture: the byte-wide 0x00-0x09 register protocol ----
		uint8_t hostRead(const uint16_t _reg) const
		{
			const int32_t value = readIram(m_hostReadAddr);
			switch(_reg)
			{
			case HostDataMid:	return 0x00;	// ready
			case HostAddrLo:	return static_cast<uint8_t>(value);
			case HostAddrHi:	return static_cast<uint8_t>(value >> 8);
			case HostDataLo:	return static_cast<uint8_t>(value >> 16);
			default:			return 0x00;
			}
		}

		void hostWrite(const uint16_t _reg, const uint8_t _value)
		{
			switch(_reg)
			{
			case HostDataLo:  m_hostLatch = (m_hostLatch & 0xffff00u) | _value; return;
			case HostDataMid: m_hostLatch = (m_hostLatch & 0xff00ffu) | (_value << 8); return;
			case HostDataHi:  m_hostLatch = (m_hostLatch & 0x00ffffu) | (_value << 16); return;
			case HostAddrHi:
			case HostReadHi:  m_hostReadAddr = static_cast<uint16_t>((m_hostReadAddr & 0x00ff) | (_value << 8)); return;
			case HostReadLo:  m_hostReadAddr = static_cast<uint16_t>((m_hostReadAddr & 0xff00) | _value); return;
			case HostConfig:  applyConfig(static_cast<uint16_t>(m_hostLatch)); return;
			case HostAddrLo:
				m_hostReadAddr = static_cast<uint16_t>((m_hostReadAddr & 0xff00) | _value);
				writeIram(m_hostReadAddr, static_cast<int32_t>(m_hostLatch & 0xffffff));
				return;
			default:
				return;
			}
		}

		// Direct IRAM access: the ring below the program base, program words above.
		void writeIram(const uint16_t _addr, const int32_t _word)
		{
			const uint16_t a = _addr & (HostIramSize - 1);
			if(a < IramProgramBase)
				m_runtime->iram[a] = _word & 0xffffff;
			else
				m_program->write(a - IramProgramBase, _word);
		}

		int32_t readIram(const uint16_t _addr) const
		{
			const uint16_t a = _addr & (HostIramSize - 1);
			return a < IramProgramBase ? m_runtime->iram[a] : m_program->read(a - IramProgramBase);
		}

		uint16_t config() const { return m_config; }
		bool running() const { return m_running; }

		// Adopt a changed program. Call once per sample before runProgram().
		void checkTaint()
		{
			if(!m_program->tainted())
				return;
			kickCompile();
		}

		// Make a staged program take effect now. The switch is immediate on the
		// interpreter; the JIT emission is deferred to the worker.
		void cacheProgram() { kickCompile(); }

		bool     programTainted() const { return m_program->tainted(); }

		void runProgram()
		{
			pollCompile();
			if(m_canJit)
				m_jit->runProgram();
			else
				LSPInterpreter::runProgram(*m_program, *m_runtime);
		}

		// One stereo frame in, one out; halted or unprogrammed, the chip is silent.
		SampleFrame process(const SampleFrame _in)
		{
			if(!m_running || !hasProgram())
				return {0, 0};
			m_runtime->audioInL = _in.first;
			m_runtime->audioInR = _in.second;
			checkTaint();
			runProgram();
			return { m_runtime->audioOutL, m_runtime->audioOutR };
		}

		void setAudioIn(const int32_t _l, const int32_t _r)
		{
			m_runtime->audioInL = _l;
			m_runtime->audioInR = _r;
		}
		SampleFrame audioOut() const { return { m_runtime->audioOutL, m_runtime->audioOutR }; }

		// True once the host has written a program worth running.
		bool hasProgram() const { return m_program->hasProgram; }

		LSPRuntime&       runtime()       { return *m_runtime; }
		const LSPRuntime& runtime() const { return *m_runtime; }
		LSPProgram&       program()       { return *m_program; }
		const LSPProgram& program() const { return *m_program; }

	private:
		// The firmware brackets program uploads through HostConfig: 0x0001
		// starts, 0x1021 halts. Unknown values must not invent a run-state edge.
		void applyConfig(const uint16_t _config)
		{
			m_config = _config;
			switch(_config)
			{
			case 0x0001:
				if(m_program->tainted())
					cacheProgram();
				m_running = true;
				break;
			case 0x1021:
				m_running = false;
				break;
			default:
				break;
			}
		}

		void fillSnapshot(Snapshot& _snapshot) const
		{
			std::memcpy(_snapshot.instr.data(), m_program->instr, sizeof(LSPInstr) * ProgramWords);
		}

		// The program changed: re-decode now (the deterministic switch) and compile it in the background.
		void kickCompile()
		{
			if(m_program->tainted())
				m_program->cacheProgram();
			m_canJit = false;
			m_compiler.kick([this](Snapshot& _s) { fillSnapshot(_s); });
		}

		void pollCompile()
		{
			bool ok = false;
			if(m_compiler.poll([this](Snapshot& _s) { fillSnapshot(_s); }, ok))
				m_canJit = ok;
		}

		bool     m_canJit = false;
		uint16_t m_config = 0;
		bool     m_running = false;
		uint32_t m_hostLatch = 0;
		uint16_t m_hostReadAddr = 0;

		std::unique_ptr<LSPRuntime> m_runtime;
		std::unique_ptr<LSPProgram> m_program;
		std::unique_ptr<LSPJIT>     m_jit;

		// Declared after the back end so that it is destroyed, and its thread joined, first.
		chips::JitCompileWorker<LSPJIT::Compile, Snapshot> m_compiler;
	};
}

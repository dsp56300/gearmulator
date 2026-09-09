#pragma once

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "lsp_interpreter.h"
#include "lsp_jit.h"

namespace lspLib
{
	// Host interface and background JIT compilation, with interpreter fallback.
	class LSPDispatcher
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;	// left, right

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
		{
			m_reqCache.resize(ProgramWords);
			m_workCache.resize(ProgramWords);
			m_worker = std::thread([this] { workerLoop(); });
		}

		~LSPDispatcher()
		{
			{
				std::lock_guard<std::mutex> lk(m_mx);
				m_exit = true;
			}
			m_cv.notify_one();
			if(m_worker.joinable())
				m_worker.join();
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

		// ---- background compile ----
		// m_canJit / m_compileInFlight / m_kickGen are audio-thread-only; the
		// request cache and generation are exchanged under m_mx; the worker
		// compiles from its private copy while the audio thread keeps
		// patching the live tables and runs the interpreter until the
		// release/acquire handshake on m_done publishes the fresh code.
		void workerLoop()
		{
			std::unique_lock<std::mutex> lk(m_mx);
			uint64_t lastGen = 0;
			for(;;)
			{
				m_cv.wait(lk, [&] { return m_exit || m_reqGen != lastGen; });
				if(m_exit)
					return;
				const uint64_t gen = m_reqGen;
				std::memcpy(m_workCache.data(), m_reqCache.data(), sizeof(LSPInstr) * ProgramWords);
				lk.unlock();

				bool ok = false;
				try { ok = m_jit->compile(m_workCache.data()); }
				catch(...) { ok = false; }
				lastGen = gen;
				m_done.store((gen << 1) | (ok ? 1u : 0u), std::memory_order_release);

				lk.lock();
			}
		}

		void issueRequest()
		{
			{
				std::lock_guard<std::mutex> lk(m_mx);
				std::memcpy(m_reqCache.data(), m_program->instr, sizeof(LSPInstr) * ProgramWords);
				m_reqGen = m_kickGen;
			}
			m_cv.notify_one();
			m_reqGenIssued = m_kickGen;
		}

		// The program changed: re-decode now (the deterministic switch) and
		// hand a snapshot to the worker. Kicks during a compile coalesce.
		void kickCompile()
		{
			if(m_program->tainted())
				m_program->cacheProgram();
			m_canJit = false;
			++m_kickGen;
			if(m_compileInFlight)
			{
				m_rekickPending = true;
				return;
			}
			m_compileInFlight = true;
			issueRequest();
		}

		// Adopt a finished compile; only the latest generation flips m_canJit.
		void pollCompile()
		{
			if(!m_compileInFlight)
				return;
			const uint64_t done = m_done.load(std::memory_order_acquire);
			if((done >> 1) != m_reqGenIssued)
				return;
			if(m_rekickPending)
			{
				m_rekickPending = false;
				issueRequest();
				return;
			}
			if((done >> 1) == m_kickGen)
			{
				m_compileInFlight = false;
				m_canJit = (done & 1) != 0;
			}
		}

		bool     m_canJit = false;
		uint16_t m_config = 0;
		bool     m_running = false;
		uint32_t m_hostLatch = 0;
		uint16_t m_hostReadAddr = 0;

		std::unique_ptr<LSPRuntime> m_runtime;
		std::unique_ptr<LSPProgram> m_program;
		std::unique_ptr<LSPJIT>     m_jit;

		std::vector<LSPInstr>   m_reqCache;
		std::vector<LSPInstr>   m_workCache;
		uint64_t                m_kickGen = 0;
		uint64_t                m_reqGenIssued = 0;
		bool                    m_rekickPending = false;
		uint64_t                m_reqGen = 0;
		bool                    m_compileInFlight = false;
		std::atomic<uint64_t>   m_done { 0 };
		std::mutex              m_mx;
		std::condition_variable m_cv;
		bool                    m_exit = false;
		std::thread             m_worker;
	};
}

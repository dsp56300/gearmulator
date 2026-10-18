#pragma once

// ESP dispatcher, the board-facing class. The interpreter and the native kernels
// share one Runtime image, so engines switch between frames without a click.
//
// Board contract, per sample:
//   1. stage inputs with writeGRAM (or a receive-input provider)
//   2. runProgram(clocks) then sync()
//   3. read outputs with readGRAM and replay dintEdges into the MCU
// Host writes are accepted between frames only (doc/esp/README.md).
//
// Kernels compile on a background worker from a snapshot of the program. Until
// one lands, the sequential interpreter runs the frame with the kernel's
// scheduling, so the output does not depend on compile speed.

#include "esp_interpreter.h"
#include "esp_jit.h"
#include "esp_program.h"

#include "custom_chips/jitCompileWorker.h"

#include <memory>

namespace esp
{
	enum class Engine { Interpreter, Jit };

	template <int lg2eramSize>
	class Dispatcher
	{
	public:
		using RuntimeT = Runtime<lg2eramSize>;
		using InterpreterT = Interpreter<lg2eramSize>;
		using ReceiveInput = typename InterpreterT::ReceiveInput;
		using DintCallback = typename InterpreterT::DintCallback;
		using Engine = esp::Engine;

		struct Statistics { uint64_t interpreted = 0, native = 0, compiles = 0, adopted = 0, failed = 0; };

		Dispatcher()
			: m_runtime(std::make_unique<RuntimeT>())
			, m_interpreter(m_runtime.get())
			, m_engine(CompileMode == chips::JitCompile::None ? Engine::Interpreter : Engine::Jit)
			, m_compiler([](const CompileRequest& request) { return compile(request); })
		{
		}

		Dispatcher(const Dispatcher&) = delete;
		Dispatcher& operator=(const Dispatcher&) = delete;

		void reset()
		{
			m_interpreter.reset();
			m_seenStructure = ~0ull;
			m_quietFrames = 0;
		}

		void setEngine(Engine engine) { m_engine = engine; }
		Engine engine() const { return m_engine; }
		// Compile on the calling thread (tests, deterministic tools).
		void setBackgroundCompile(bool background) { m_compiler.setSynchronous(!background); }
		const Statistics& statistics() const { return m_stats; }
		bool kernelReady() const
		{
			return m_engine == Engine::Jit && m_compiled && m_compiled->kernel &&
				m_compiled->key.structure == m_interpreter.structureGeneration() &&
				m_compiled->key.configuration == compilationConfiguration();
		}

		// Host bus (MCU side).
		void hostWrite(uint32_t address, uint8_t value) { m_interpreter.hostWrite(address, value); }
		uint8_t hostRead(uint32_t address) const { return m_interpreter.hostRead(address); }
		uint32_t readHostReg() const { return m_interpreter.readHostReg(); }
		void writePMem(uint16_t byteAddress, uint8_t value) { m_interpreter.writePMem(byteAddress, value); }
		void writePMem32(uint16_t wordIndex, uint32_t value) { m_interpreter.writePMem32(wordIndex, value); }
		uint8_t readPRAM(size_t byteOffset) const { return m_interpreter.readPRAM(byteOffset); }
		uint8_t config(unsigned index) const { return m_interpreter.config(index); }

		void runProgram(uint32_t clocks = FRAME_CLOCKS)
		{
			if (m_engine == Engine::Jit)
			{
				adoptCompile(clocks);
				runJit(clocks);
				return;
			}
			m_interpreter.runProgram(clocks);
			++m_stats.interpreted;
		}

		void sync() { m_interpreter.sync(); }

		// Board I/O on the shared image.
		void writeGRAM(int32_t value, uint8_t offset) { m_runtime->writeGram(value, offset); }
		int32_t readGRAM(uint8_t offset) const { return m_runtime->readGram(offset); }
		int32_t readIRAM0(uint8_t offset) const { return m_runtime->core[0].readIram(offset); }
		int32_t readIRAM1(uint8_t offset) const { return m_runtime->core[1].readIram(offset); }
		void writeIRAM0(int32_t value, uint8_t offset) { m_runtime->core[0].writeIram(value, offset); }
		void writeIRAM1(int32_t value, uint8_t offset) { m_runtime->core[1].writeIram(value, offset); }

		// Interrupt pins: current levels, and the edges of the last frame.
		uint8_t readDint() const { return m_interpreter.readDint(); }
		unsigned dintEdgeCount() const { return m_interpreter.dintEdgeCount(); }
		const DintEdge& dintEdge(unsigned i) const { return m_interpreter.dintEdge(i); }
		void drainDintEdges() { m_interpreter.drainDintEdges(); }
		void setDintCallback(DintCallback callback) { m_interpreter.setDintCallback(std::move(callback)); }
		void setDintFrameCycles(uint32_t cycles) { m_interpreter.setDintFrameCycles(cycles); }

		void setExternalClockPresent(bool present) { m_interpreter.setExternalClockPresent(present); }
		void setReceiveInput(ReceiveInput input) { m_interpreter.setReceiveInput(std::move(input)); }
		void setEramCompression(bool compress) { m_interpreter.setEramCompression(compress); }
		bool isEramCompression() const { return m_interpreter.isEramCompression(); }

		RuntimeT& runtime() { return *m_runtime; }
		const RuntimeT& runtime() const { return *m_runtime; }
		InterpreterT& interpreter() { return m_interpreter; }
		const InterpreterT& interpreter() const { return m_interpreter; }

	private:
		static constexpr chips::JitCompile CompileMode = CHIPS_JIT_ASMJIT ? chips::JitCompile::Worker : chips::JitCompile::None;
		static constexpr unsigned kSettleFrames = 64;

		// A kernel is a pure function of its key: coefficients and addresses are read live.
		struct CompileKey
		{
			uint64_t structure = 0;
			uint32_t clocks = 0, configuration = 0;
			bool operator==(const CompileKey& other) const
			{
				return structure == other.structure && clocks == other.clocks && configuration == other.configuration;
			}
		};
		struct CompileRequest
		{
			CompileKey key;
			ProgramSnapshot program;
		};
		struct Compiled
		{
			CompileKey key;
			KernelFn kernel = nullptr;
			~Compiled();
		};
		using CompiledPtr = std::unique_ptr<Compiled>;

		// Defined in esp_jit_backend.h, instantiated in esp.cpp.
		static CompiledPtr compile(const CompileRequest& request);

		std::unique_ptr<RuntimeT> m_runtime;
		InterpreterT m_interpreter;
		Engine m_engine;
		Statistics m_stats;
		CompiledPtr m_compiled;
		CompileKey m_requestedKey;
		uint64_t m_seenStructure = ~0ull;
		unsigned m_quietFrames = 0;
		// Last, so its thread is joined before anything a compile or a retired kernel touches goes away.
		chips::JitCompileWorker<CompileMode, CompileRequest, CompiledPtr> m_compiler;

		uint32_t compilationConfiguration() const
		{
			const auto& eram = m_runtime->eram;
			return uint32_t(eram.memoryMode) | (uint32_t(eram.compress) << 8) |
				(uint32_t(eram.staticBus) << 9) |
				(uint32_t(m_interpreter.conditionalBranchesEnabled()) << 10) |
				(uint32_t(eram.commandSpacing()) << 16);
		}
		CompileKey compileKey(uint32_t clocks) const
		{
			return {m_interpreter.structureGeneration(), clocks, compilationConfiguration()};
		}
		void fillRequest(CompileRequest& request, uint32_t clocks) const
		{
			request.key = compileKey(clocks);
			request.program.capture(m_interpreter);
		}

		void runJit(uint32_t clocks)
		{
			if constexpr (CompileMode != chips::JitCompile::None)
			{
				const auto generation = m_interpreter.structureGeneration();
				if (generation != m_seenStructure)
				{
					m_seenStructure = generation;
					m_quietFrames = 0;
				}
				else if (m_quietFrames < kSettleFrames)
					++m_quietFrames;
				const auto& eram = m_runtime->eram;
				if (m_quietFrames >= kSettleFrames && clocks <= FRAME_CLOCKS && !eram.active() && !eram.deferredCommand &&
					eram.nextCommandCycle <= eram.cycle && m_interpreter.batchedFrameReady())
				{
					const auto key = compileKey(clocks);
					if (!m_compiled || !(m_compiled->key == key))
						requestCompile(key, clocks);
					if (m_compiled && m_compiled->key == key && m_compiled->kernel)
					{
						KernelFrame frame;
						frame.image = makeJitFrame(m_interpreter);
						m_compiled->kernel(&frame);
						m_interpreter.finishBatchedFrame(clocks);
						++m_stats.native;
						return;
					}
					m_interpreter.runSequential(clocks);
					++m_stats.interpreted;
					return;
				}
			}
			// Uploads, settling and frames a kernel cannot enter run in lockstep.
			m_interpreter.runProgram(clocks);
			++m_stats.interpreted;
		}

		void requestCompile(const CompileKey& key, uint32_t clocks)
		{
			if (m_compiler.inFlight() && m_requestedKey == key)
				return;
			m_requestedKey = key;
			++m_stats.compiles;
			m_compiler.kick([&](CompileRequest& request) { fillRequest(request, clocks); });
			if (m_compiler.synchronous())
				adoptCompile(clocks);
		}
		void adoptCompile(uint32_t clocks)
		{
			if (!m_compiler.inFlight())
				return;
			CompiledPtr result;
			if (!m_compiler.poll([&](CompileRequest& request) { fillRequest(request, clocks); }, result))
				return;
			if (!result)
			{
				// The compile itself threw; keep it as a failed kernel so it is not retried every frame.
				result = std::make_unique<Compiled>();
				result->key = m_requestedKey;
			}
			if (!(result->key == compileKey(clocks)))
			{
				m_compiler.retire(std::move(result));
				return;
			}
			if (result->kernel)
				++m_stats.adopted;
			else
				++m_stats.failed;
			if (m_compiled)
				m_compiler.retire(std::move(m_compiled));
			m_compiled = std::move(result);
		}
	};

	extern template class Dispatcher<0>;
	extern template class Dispatcher<17>;
	extern template class Dispatcher<18>;
	extern template class Dispatcher<19>;
}

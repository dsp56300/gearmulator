#pragma once

// The JIT interface shared by the backends and the dispatcher. A compiled frame function executes one whole
// frame of one chip, or of two lockstep-linked chips, from the lowered programs (xp_dsp_flat.h): it reads and
// writes the DspState and DspParams it is handed at run time, never calls out, and switches with the flat
// interpreter at any frame boundary. The C++ frame driver keeps doing what it does around the interpreter
// (dspOps::beginFrame / endFrame, the IRAM3 ramps, the serial staging); the function covers everything the
// interpreter's executeSlot/endFrame pair covers, deposits included.

#include "xp_dsp_program.h"
#include "xp_dsp_state.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace xpLib
{
	// Run-time arguments of a compiled frame function. Index 0 is the first chip, 1 the linked partner.
	struct DspJitFrame
	{
		DspState* state[2] = {nullptr, nullptr};
		const DspParams* params[2] = {nullptr, nullptr};
		const DspMixerFrame* mixer[2] = {nullptr, nullptr}; // null: the deposits were hoisted, or there are none
		uint32_t* mixerBank[2] = {nullptr, nullptr};        // the IRAM bank the mixer deposits into this frame
		uint32_t* processingBank[2] = {nullptr, nullptr};   // the bank the program reads at memaddr 40-7f
		// Scratch of the generated code.
		uint64_t returnAddress = 0;
		uint32_t pc[2] = {0, 0};
		int32_t linkWord[2] = {0, 0};
		uint8_t linkFlag[2] = {0, 0};
		uint8_t portBusy[2] = {0, 0};
	};

	using DspJitRun = void (*)(const DspJitFrame*);
} // namespace xpLib

#if defined(_M_X64) || defined(__x86_64__) || defined(__x86_64) || defined(__amd64__)
#	include "xp_dsp_jit_x86.h"
#elif defined(__aarch64__) || defined(__ARM_ARCH_8) || defined(_M_ARM64)
#	include "xp_dsp_jit_arm64.h"
#else
#	error "Unsupported architecture for the XP DSP JIT"
#endif

// Background compilation of frame functions with the CSP/LSP handshake: the frame driver asks for the code
// of a program key every frame; while the compiled code does not match, it keeps the flat interpreter and a
// worker thread compiles from a snapshot. Only the latest requested generation is adopted, kicks during a
// compile coalesce, and the run pointer is handed out only after a release/acquire handshake.
namespace xpLib
{
	class DspJitDispatcher
	{
	public:
		DspJitDispatcher()
		{
			for (FlatProgram* program : {&m_requestA, &m_requestB, &m_workA, &m_workB})
				program->ops.reserve(dsp::nProgramSlots * 16);
			m_worker = std::thread([this] { workerLoop(); });
		}

		~DspJitDispatcher()
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_exit = true;
			}
			m_condition.notify_one();
			m_worker.join();
		}

		DspJitDispatcher(const DspJitDispatcher&) = delete;
		DspJitDispatcher& operator=(const DspJitDispatcher&) = delete;

		// The frame function for _key, or nullptr while it is not compiled yet. _a and _b are the live
		// lowered programs the key stands for; a compile is requested when none is in flight for the key.
		DspJitRun acquire(const uint64_t _key, const FlatProgram& _a, const FlatProgram* _b)
		{
			pollCompile(_a, _b);
			if (m_canRun && m_activeKey == _key)
				return m_backend.run();
			if (m_kickKey != _key || !m_compileInFlight)
				kick(_key, _a, _b);
			return nullptr;
		}

	private:
		void workerLoop()
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			uint64_t lastGeneration = 0;
			for (;;)
			{
				m_condition.wait(lock, [&] { return m_exit || m_requestGeneration != lastGeneration; });
				if (m_exit)
					return;
				const auto generation = m_requestGeneration;
				m_workA = m_requestA;
				m_workHasB = m_requestHasB;
				if (m_workHasB)
					m_workB = m_requestB;
				lock.unlock();

				bool ok = false;
				try
				{
					ok = m_backend.compile(m_workA, m_workHasB ? &m_workB : nullptr);
				}
				catch (...)
				{
					ok = false;
				}
				lastGeneration = generation;
				m_done.store((generation << 1) | (ok ? 1u : 0u), std::memory_order_release);
				lock.lock();
			}
		}

		void issueRequest(const FlatProgram& _a, const FlatProgram* _b)
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_requestA = _a;
				m_requestHasB = _b != nullptr;
				if (_b != nullptr)
					m_requestB = *_b;
				m_requestGeneration = m_kickGeneration;
			}
			m_condition.notify_one();
			m_requestGenerationIssued = m_kickGeneration;
		}

		// The program changed: the running code is stale from now on; hand a snapshot to the worker. Kicks
		// during a compile coalesce into one re-kick with the latest programs.
		void kick(const uint64_t _key, const FlatProgram& _a, const FlatProgram* _b)
		{
			m_canRun = false;
			m_kickKey = _key;
			++m_kickGeneration;
			if (m_compileInFlight)
			{
				m_rekickPending = true;
				return;
			}
			m_compileInFlight = true;
			issueRequest(_a, _b);
		}

		void pollCompile(const FlatProgram& _a, const FlatProgram* _b)
		{
			if (!m_compileInFlight)
				return;
			const auto done = m_done.load(std::memory_order_acquire);
			if ((done >> 1) != m_requestGenerationIssued)
				return;
			if (m_rekickPending)
			{
				m_rekickPending = false;
				issueRequest(_a, _b);
				return;
			}
			if ((done >> 1) == m_kickGeneration)
			{
				m_compileInFlight = false;
				m_canRun = (done & 1) != 0;
				m_activeKey = m_kickKey;
			}
		}

		DspJitBackend m_backend;
		bool m_canRun = false;
		uint64_t m_activeKey = ~uint64_t{0};
		uint64_t m_kickKey = ~uint64_t{0};
		uint64_t m_kickGeneration = 0;
		uint64_t m_requestGenerationIssued = 0;
		bool m_compileInFlight = false;
		bool m_rekickPending = false;

		FlatProgram m_requestA;
		FlatProgram m_requestB;
		bool m_requestHasB = false;
		uint64_t m_requestGeneration = 0;
		FlatProgram m_workA;
		FlatProgram m_workB;
		bool m_workHasB = false;
		std::atomic<uint64_t> m_done{0};
		std::mutex m_mutex;
		std::condition_variable m_condition;
		bool m_exit = false;
		std::thread m_worker;
	};
} // namespace xpLib

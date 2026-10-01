#pragma once

// The JIT interface shared by the backends and the dispatcher. A compiled frame function executes one whole
// frame of one chip, or of two lockstep-linked chips, from the lowered programs (xp_dsp_program.h): it reads and
// writes the DspState and DspParams it is handed at run time, never calls out, and switches with the flat
// interpreter at any frame boundary. The C++ frame driver keeps doing what it does around the interpreter
// (dspOps::beginFrame / endFrame, the IRAM3 ramps, the serial staging); the function covers everything the
// interpreter's executeSlot/endFrame pair covers, deposits included.

#include "xp_dsp_program.h"
#include "xp_dsp_state.h"

#include "../jitCompileWorker.h"

#include <cstdint>

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

// The back end of this host (../jitHost.h). Each defines DspJitBackend with the same interface;
// DspJitBackend::Compile tells the dispatcher how it gets its code compiled, if at all.
#include "../jitHost.h"
#if CHIPS_JIT_X86_64
#	include "xp_dsp_jit_x86.h"
#elif CHIPS_JIT_ARM64
#	include "xp_dsp_jit_arm64.h"
#elif CHIPS_JIT_WASM
#	include "xp_dsp_jit_wasm.h"
#else
#	include "xp_dsp_jit_none.h"
#endif

// The frame driver asks for the code of a program key every frame; while the compiled code does not match, it
// keeps the flat interpreter and the program is compiled from a snapshot (../jitCompileWorker.h).
namespace xpLib
{
	class DspJitDispatcher
	{
	public:
		DspJitDispatcher()
			: m_compiler([this](const Snapshot& _s) { return m_backend.compile(_s.a, _s.hasB ? &_s.b : nullptr); },
				[this] { return chips::pollHostBackend(m_backend); })
		{
		}

		DspJitDispatcher(const DspJitDispatcher&) = delete;
		DspJitDispatcher& operator=(const DspJitDispatcher&) = delete;

		// The frame function for _key, or nullptr while it is not compiled yet. _a and _b are the live
		// lowered programs the key stands for; a compile is requested when none is in flight for the key.
		DspJitRun acquire(const uint64_t _key, const FlatProgram& _a, const FlatProgram* _b)
		{
			if (!DspJitBackend::Available)
				return nullptr;
			const auto fill = [&](Snapshot& _s) { _s.set(_a, _b); };
			bool ok = false;
			if (m_compiler.poll(fill, ok))
			{
				m_canRun = ok;
				m_activeKey = m_kickKey;
			}
			if (m_activeKey == _key && !m_compiler.inFlight())
			{
				// Compiled, or turned down: a program the back end cannot compile is not tried again
				// until it changes.
				return m_canRun ? m_backend.run() : nullptr;
			}
			if (m_kickKey != _key || !m_compiler.inFlight())
			{
				m_canRun = false;
				m_kickKey = _key;
				m_compiler.kick(fill);
			}
			return nullptr;
		}

	private:
		struct Snapshot
		{
			Snapshot()
			{
				a.ops.reserve(dsp::nProgramSlots * 16);
				b.ops.reserve(dsp::nProgramSlots * 16);
			}

			void set(const FlatProgram& _a, const FlatProgram* _b)
			{
				a = _a;
				hasB = _b != nullptr;
				if (_b)
					b = *_b;
			}

			FlatProgram a;
			FlatProgram b;
			bool hasB = false;
		};

		DspJitBackend m_backend;
		bool m_canRun = false;
		uint64_t m_activeKey = ~uint64_t{0};
		uint64_t m_kickKey = ~uint64_t{0};

		// Declared after the back end so that it is destroyed, and its thread joined, first.
		chips::JitCompileWorker<DspJitBackend::Compile, Snapshot> m_compiler;
	};
} // namespace xpLib

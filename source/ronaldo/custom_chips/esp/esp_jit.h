#pragma once

// Native frame ABI: what a kernel reads besides the runtime image. Live coefficients,
// shifts, addresses and host-tail data need no recompile.

#include "esp_program.h"
#include "custom_chips/jitHost.h"

#include <cstddef>

namespace esp
{
	// Everything a kernel needs besides the runtime image, filled per call.
	struct JitFrame
	{
		void* runtime;					// Runtime<lg2eramSize>*
		int32_t* iram[2];
		int32_t* gram;
		int32_t* eramMemory;			// nullptr without ERAM
		uint16_t* eramPacked;			// compressed-mode shadow codes
		const int16_t* coef16[2];
		const uint8_t* shift[2];
		const uint32_t* eramImm;
		const uint32_t* words[2];
		int32_t hostTail[2];
		uint32_t advanceMemory;			// tail 00/FF (1) or 00/00 (0)
	};
	struct KernelFrame
	{
		JitFrame image;
		uint8_t requests[FRAME_CLOCKS];	// core 1's GRAM requests, cleared by kernels that arbitrate
	};
	using KernelFn = void (*)(KernelFrame*);

	// Offsets into the runtime image the emitters address directly.
	template <int lg2eramSize>
	struct RuntimeLayout
	{
		using R = Runtime<lg2eramSize>;
		using E = EramRuntime<lg2eramSize>;
		static constexpr size_t mulcoeffs = offsetof(R, mulcoeffs);
		static constexpr size_t mailbox = offsetof(R, mailbox);
		static constexpr size_t eram = offsetof(R, eram);
		static constexpr size_t readLatch = eram + offsetof(E, readLatch);
		static constexpr size_t writeLatch = eram + offsetof(E, writeLatch);
		static constexpr size_t varOffset = eram + offsetof(E, varOffset);
		static constexpr size_t cycle = eram + offsetof(E, cycle);
	};

	template <int lg2eramSize>
	JitFrame makeJitFrame(Base<lg2eramSize>& base)
	{
		auto& rt = base.runtime();
		JitFrame f{};
		f.runtime = &rt;
		// Ring bases at the frame's position: slot mem is base[mem], no wrap.
		f.iram[0] = rt.core[0].iram + rt.core[0].iramPos; f.iram[1] = rt.core[1].iram + rt.core[1].iramPos;
		f.gram = rt.gram + rt.core[0].iramPos;
		f.eramMemory = lg2eramSize ? rt.eram.memory.get() : nullptr;
		f.eramPacked = lg2eramSize ? rt.eram.packed.get() : nullptr;
		f.coef16[0] = base.coefficientTable(0); f.coef16[1] = base.coefficientTable(1);
		f.shift[0] = base.shiftTable(0); f.shift[1] = base.shiftTable(1);
		f.eramImm = base.eramImmediateTable();
		f.words[0] = base.wordsOf(0); f.words[1] = base.wordsOf(1);
		f.hostTail[0] = base.hostTail(0); f.hostTail[1] = base.hostTail(1);
		f.advanceMemory = !(base.config(1) & 0x40);
		return f;
	}
}

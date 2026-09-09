#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace xpLib
{
	// Sizes of the programmable DSP's memories and word streams.
	namespace dsp
	{
		constexpr size_t nProgramSlots = 288;
		constexpr size_t nExecutionSlots = 256;
		constexpr size_t nIramSlots = 64;
		constexpr size_t nEramWords = 0x10000;
		constexpr size_t nSerialWords = 32;
		constexpr size_t nSerialBuses = 4;
		constexpr uint16_t alternateBcdPacking_3932 = 0x0020;
	} // namespace dsp

	enum class DspSerialBus : uint8_t
	{
		a,
		b,
		c,
		d,
	};

	// The host-visible program: 288 PRAM words (28 bits used) with a parallel 16-bit CRAM entry each.
	struct DspProgram
	{
		std::array<uint32_t, dsp::nProgramSlots> pram{};
		std::array<uint16_t, dsp::nProgramSlots> cram{};
	};

	// No default member initializers: the mixer stage builds a frame every
	// sample and writes every used entry, so it declares the array without
	// value-initializing 4 KB first ({} still zero-fills where wanted).
	struct DspMixerSend
	{
		size_t destination;
		int64_t contribution;
	};
	using DspMixerFrame = std::array<std::array<DspMixerSend, 4>, 64>;

	// The sends of a frame summed per destination cell, in deposit order, with the cells that received any
	// send and the cells whose running sum left the 24-bit range at some send (those would have saturated on
	// the chip and are replayed send by send). Built by the mixer stage while it computes the contributions.
	struct DspMixerSummary
	{
		std::array<int64_t, dsp::nIramSlots> sums{};
		uint64_t seen = 0;
		uint64_t clipped = 0;

		void add(const size_t _destination, const int64_t _contribution)
		{
			if (_destination >= dsp::nIramSlots)
				return;
			const auto bit = uint64_t{1} << _destination;
			// A zero send changes nothing once the cell has been cleared by its first send.
			if (_contribution == 0 && (seen & bit) != 0)
				return;
			seen |= bit;
			auto& sum = sums[_destination];
			sum += _contribution;
			if (sum < -0x800000 || sum > 0x7fffff)
				clipped |= bit;
		}
	};

	// The runtime-editable numbers of a program, kept out of the decoded instructions so a host write can
	// patch a value in place instead of invalidating the program: functions 1-C store their coefficient
	// (sext14 << shift), D its 29-bit logical immediate, E/F their decoded immediate.
	struct DspParams
	{
		std::array<int32_t, dsp::nProgramSlots> param{};
		// ERAM offset bits per slot, patched in place by offset-only PRAM writes: the slot's own high (bits
		// 6:0 as a first word) and low (bits 8:0 as a second word) fields, and the resolved 16-bit offset of a
		// statically paired command on its second word (the previous slot's high with this slot's low).
		std::array<uint8_t, dsp::nProgramSlots> eramOffsetHigh{};
		std::array<uint16_t, dsp::nProgramSlots> eramOffsetLow{};
		std::array<uint16_t, dsp::nProgramSlots> eramOffset{};
	};

	struct DspStepRequest
	{
		bool executeProgram = false;
		bool serialInputEnabled = false;
		uint16_t serialAudio0Config = 0xffff;
		uint16_t serialAudio1Config = 0xffff;
		bool serialOutputEnabled = false;
		size_t executionSlots = dsp::nExecutionSlots;
		const DspMixerFrame* mixerFrame = nullptr;
	};

	struct DspFrameContext
	{
		DspStepRequest request{};
		size_t cycle = 0;
		size_t pc = 0;
	};

	// Everything an engine reads or writes while executing a program. Trivially copyable, fixed-width
	// members at constant offsets, so generated code can address it directly. The cross-frame carry comes
	// first, the memories last.
	struct DspState
	{
		// Datapath latches carried across slots and frames.
		int64_t accumulator = 0; // sign-extended to 29 bits at every commit
		int64_t iramReadLatch = 0;
		int64_t multiplyResultLatch = 0; // saturated to 29 bits
		int64_t multiplyFeedbackLatch = 0;
		int64_t eramReadLatch = 0;
		int64_t eramPendingWriteValue = 0;
		uint16_t iram3ParameterLatch = 0;
		uint16_t eramIndexedOffset = 0;
		uint8_t eramPrefixPending = 0;
		uint8_t eramPendingWrite = 0;
		uint8_t eramOffsetHigh = 0;
		uint8_t iramSelPhase = 0;
		// The preceding multiply discarded a negative fraction (a negative exact product not divisible by its
		// power-of-two denominator); the parallel ALUs D-F subtract one or two for it. XP3-verified.
		uint8_t multiplyNegativeFraction = 0;
		struct PendingEramRead
		{
			int32_t countdown = -1;
			int32_t value = 0;
		};
		std::array<PendingEramRead, 2> pendingEramReads{};

		// Frame constants and per-frame counters.
		uint32_t eramPos = 0;
		uint32_t outputWordPosition = 0;
		uint8_t dacPortPosition = 0;
		uint8_t outputPins = 0; // bits 0/1: OUTP0/1 levels; bit 2: frame-local OUTP0-fall latch
		uint64_t mixerInitialized = 0;

		// Serial buses and the DAC copy of the B/C/D words. Only buses A and B have inputs.
		std::array<int64_t, 2> serialInputNode{};
		std::array<uint32_t, 2> serialInputCount{};
		std::array<uint32_t, 2> serialInputIndex{};
		std::array<uint32_t, dsp::nSerialBuses> serialOutputCount{};
		std::array<std::array<int32_t, dsp::nSerialWords>, 2> serialInput{};
		std::array<std::array<int32_t, dsp::nSerialWords>, dsp::nSerialBuses> serialOutput{};

		// Memories.
		std::array<uint32_t, dsp::nIramSlots> iram1{};
		std::array<uint32_t, dsp::nIramSlots> iram2{};
		std::array<uint32_t, dsp::nIramSlots> iram3{};
		std::array<uint32_t, dsp::nEramWords> eram{};
	};

	// Power-on values. The ERAM read latch starts at the most negative 24-bit value.
	inline void dspStateReset(DspState& _state)
	{
		_state = DspState{};
		_state.eramReadLatch = -0x800000;
	}
} // namespace xpLib

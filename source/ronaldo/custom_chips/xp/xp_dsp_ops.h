#pragma once

// Arithmetic and state helpers shared by every DSP engine. All of them are inline and take the shared
// DspState explicitly, so the naive, IR and (later) generated-code paths cannot drift apart on the
// fixed-point rules: 24-bit memories, a 29-bit wrapping accumulator, 29-bit saturating products, and
// truncating divisions.

#include "xp_dsp_state.h"

#include <algorithm>

namespace xpLib::dspOps
{
	inline int64_t signExtend(const uint64_t _value, const unsigned _bits)
	{
		const auto mask = (uint64_t{1} << _bits) - 1;
		const auto value = _value & mask;
		return static_cast<int64_t>((value ^ (uint64_t{1} << (_bits - 1))) - (uint64_t{1} << (_bits - 1)));
	}

	inline int64_t saturate(const int64_t _value, const unsigned _bits)
	{
		const auto minimum = -(int64_t{1} << (_bits - 1));
		const auto maximum = (int64_t{1} << (_bits - 1)) - 1;
		return std::clamp(_value, minimum, maximum);
	}

	inline uint32_t encode24(const int64_t _value) { return static_cast<uint32_t>(saturate(_value, 24)) & 0x00ffffff; }

	inline uint32_t encode26(const int64_t _value) { return static_cast<uint32_t>(_value) & 0x03ffffff; }

	inline uint8_t iram3PartitionCount(const uint16_t _serialAudio1Config)
	{
		return static_cast<uint8_t>((_serialAudio1Config >> 8) & 0x1f);
	}

	// The same count is the ramp/snap boundary when indexing the 32 ramp parameters and counts parameter
	// cells down from the top of IRAM3.
	inline uint8_t iram3ParameterReadBoundary(const uint8_t _partitionCount)
	{
		return static_cast<uint8_t>(dsp::nIramSlots - _partitionCount);
	}

	inline int64_t decodeCramImmediate(const uint16_t _cram)
	{
		auto result = signExtend(_cram & 0x7fff, 15);
		if ((_cram & 0x8000) != 0)
			result <<= 13;
		return result;
	}

	inline int64_t cramCoefficient(const uint16_t _cram)
	{
		static constexpr std::array<unsigned, 4> shifts = {0, 1, 2, 4};
		return signExtend(_cram & 0x3fff, 14) << shifts[_cram >> 14];
	}

	inline int64_t multiplyCram(const int64_t _input, const uint16_t _cram)
	{
		return saturate((_input * cramCoefficient(_cram)) / 8192, 29);
	}

	// The multiplier keeps, next to its truncated result, whether it discarded a negative fraction: a
	// negative exact product that is not a multiple of the 2^_shift denominator.
	inline bool multiplyNegativeFraction(const int64_t _numerator, const unsigned _shift)
	{
		return _numerator < 0 && (_numerator & ((int64_t{1} << _shift) - 1)) != 0;
	}

	// IRAM1/IRAM2 are the mixer's ping-pong banks: the mixer writes one while the DSP processes the other.
	inline std::array<uint32_t, dsp::nIramSlots>& mixerIram(DspState& _state)
	{
		return _state.iramSelPhase ? _state.iram2 : _state.iram1;
	}

	inline const std::array<uint32_t, dsp::nIramSlots>& mixerIram(const DspState& _state)
	{
		return _state.iramSelPhase ? _state.iram2 : _state.iram1;
	}

	inline std::array<uint32_t, dsp::nIramSlots>& processingIram(DspState& _state)
	{
		return _state.iramSelPhase ? _state.iram1 : _state.iram2;
	}

	inline const std::array<uint32_t, dsp::nIramSlots>& processingIram(const DspState& _state)
	{
		return _state.iramSelPhase ? _state.iram1 : _state.iram2;
	}

	// memaddr 00-7f: bank = bit6 XOR phase, index bits 5:0; 80-bf: bank = bit5, index 0x20 + bits 4:0;
	// c0-ff: IRAM3 index bits 5:0.
	inline int64_t readIram(const DspState& _state, const uint8_t _memaddr)
	{
		if (_memaddr < 0x80)
		{
			const auto bank = static_cast<bool>((_memaddr >> 6) ^ _state.iramSelPhase);
			return signExtend((bank ? _state.iram2 : _state.iram1)[_memaddr & 0x3f], 24);
		}
		if (_memaddr < 0xc0)
		{
			const auto& bank = (_memaddr & 0x20) != 0 ? _state.iram2 : _state.iram1;
			return signExtend(bank[0x20 + (_memaddr & 0x1f)], 24);
		}
		return signExtend(_state.iram3[_memaddr & 0x3f], 24);
	}

	inline void writeIram(DspState& _state, const uint8_t _memaddr, const int64_t _value)
	{
		if (_memaddr < 0x80)
		{
			const auto bank = static_cast<bool>((_memaddr >> 6) ^ _state.iramSelPhase);
			(bank ? _state.iram2 : _state.iram1)[_memaddr & 0x3f] = encode24(_value);
			return;
		}
		if (_memaddr < 0xc0)
		{
			auto& bank = (_memaddr & 0x20) != 0 ? _state.iram2 : _state.iram1;
			bank[0x20 + (_memaddr & 0x1f)] = encode24(_value);
			return;
		}
		_state.iram3[_memaddr & 0x3f] = encode24(_value);
	}

	inline void queueEramRead(DspState& _state, const int32_t _value, const int _countdown)
	{
		for (auto& pending : _state.pendingEramReads)
		{
			if (pending.countdown >= 0)
				continue;
			pending.value = _value;
			pending.countdown = _countdown;
			return;
		}
	}

	inline void advanceEramReads(DspState& _state)
	{
		for (auto& pending : _state.pendingEramReads)
		{
			if (pending.countdown < 0 || --pending.countdown != 0)
				continue;
			_state.eramReadLatch = pending.value;
			pending.countdown = -1;
		}
	}

	inline int64_t consumeSerialInput(DspState& _state, const DspSerialBus _bus)
	{
		const auto port = static_cast<size_t>(_bus);
		if (_state.serialInputIndex[port] < _state.serialInputCount[port])
			_state.serialInputNode[port] = _state.serialInput[port][_state.serialInputIndex[port]++];
		return _state.serialInputNode[port];
	}

	// Serial and DAC I/O. ioCtrl=1 emits the next completed-bank IRAM position on bus A; ioCtrl=2 emits it on
	// buses B/C/D in rotation, sharing the same position counter; ioCtrl 4-7 drive the OUTP pins.
	inline void executeIo(DspState& _state, const uint8_t _ioCtrl, const bool _serialStateEnabled,
						  const bool _serialOutputEnabled, const uint16_t _serialAudio0Config,
						  const uint16_t _serialAudio1Config)
	{
		if (_ioCtrl == 1)
		{
			if (_serialStateEnabled)
			{
				// ioCtrl=1 occupies the next completed-IRAM word position and emits it on bus A. It is
				// independent of the instruction's transfer field; the production programs deliberately use
				// both read-backed and transfer-free events while preserving one positional word stream.
				const auto bus = static_cast<size_t>(DspSerialBus::a);
				// SDOA framing has not been pinned down; do not apply the B/C/D-only descriptor output
				// decode to this logical word stream.
				if (_serialOutputEnabled && _state.serialOutputCount[bus] < dsp::nSerialWords)
				{
					const auto word = _state.outputWordPosition & (dsp::nIramSlots - 1);
					_state.serialOutput[bus][_state.serialOutputCount[bus]++] =
						static_cast<int32_t>(signExtend(processingIram(_state)[word], 24));
				}
				++_state.outputWordPosition;
			}
		}
		else if (_ioCtrl == 2)
		{
			if (_serialStateEnabled)
			{
				// Every three-event group is assigned to B, C, and D. The exact SC-88Pro 0x5040/0x0010
				// configuration follows the same ordinal on XP3. All buses share the completed-IRAM counter
				// with ioCtrl=1.
				const auto bus = static_cast<size_t>(DspSerialBus::b) + _state.dacPortPosition;
				const auto word = _state.outputWordPosition & (dsp::nIramSlots - 1);
				const auto descriptor = _state.dacPortPosition == 0 ? static_cast<uint8_t>(_serialAudio0Config >> 8)
																	: static_cast<uint8_t>(_serialAudio1Config);
				if (_serialOutputEnabled && (descriptor & 0xc0) != 0 && _state.serialOutputCount[bus] < dsp::nSerialWords)
					_state.serialOutput[bus][_state.serialOutputCount[bus]++] =
						static_cast<int32_t>(signExtend(processingIram(_state)[word], 24));
				++_state.dacPortPosition;
				++_state.outputWordPosition;
				if (_state.dacPortPosition == 3)
					_state.dacPortPosition = 0;
			}
		}
		else if (_ioCtrl >= 4)
		{
			// Low bits directly drive OUTP0/OUTP1. OUTP2 is cleared at each frame boundary and latches the
			// first falling edge of OUTP0.
			const auto levels = static_cast<uint8_t>(_ioCtrl & 3);
			if ((_state.outputPins & 1) != 0 && (levels & 1) == 0)
				_state.outputPins |= 4;
			_state.outputPins = static_cast<uint8_t>((_state.outputPins & 4) | levels);
		}
	}

	// Mixer deposits. A destination is cleared only on its first send of a frame; even a zero-level send
	// therefore writes zero, while genuinely unreferenced IRAM slots retain their value.
	inline void beginMixerFrame(DspState& _state) { _state.mixerInitialized = 0; }

	inline void replaceMixer(DspState& _state, const size_t _destination, const int64_t _value)
	{
		if (_destination >= dsp::nIramSlots)
			return;
		mixerIram(_state)[_destination] = encode24(_value);
		_state.mixerInitialized |= uint64_t{1} << _destination;
	}

	inline void accumulateMixer(DspState& _state, const size_t _destination, const int64_t _value)
	{
		if (_destination >= dsp::nIramSlots)
			return;
		if ((_state.mixerInitialized & (uint64_t{1} << _destination)) == 0)
			replaceMixer(_state, _destination, 0);
		const auto current = signExtend(mixerIram(_state)[_destination], 24);
		mixerIram(_state)[_destination] = encode24(current + _value);
	}

	// XP3 places send A at cycle 4v, sends B and C at 4v+2, and send D at 4v+3.
	inline void stepMixerCycle(DspState& _state, const DspMixerFrame& _frame, const size_t _cycle)
	{
		const auto voice = _cycle >> 2;
		const auto phase = _cycle & 3;
		const auto deposit = [&_state, &_frame, voice](const size_t _send)
		{
			const auto& event = _frame[voice][_send];
			accumulateMixer(_state, event.destination, event.contribution);
		};
		if (phase == 0)
			deposit(0);
		else if (phase == 2)
		{
			deposit(1);
			deposit(2);
		}
		else if (phase == 3)
			deposit(3);
	}

	inline void beginFrame(DspState& _state, DspFrameContext& _context, const DspStepRequest& _request)
	{
		_context = {_request, 0, 0};
		_context.request.executionSlots = std::min(_request.executionSlots, dsp::nExecutionSlots);
		_state.serialInputIndex.fill(0);
		_state.serialOutputCount.fill(0);
		_state.outputWordPosition = 0;
		_state.dacPortPosition = 0;
		_state.outputPins &= 3;
	}

	// ERAM advances after each completed running pass; IRAM selection tracks mixer-bank sample parity on
	// every sample, including while the program is stopped.
	inline void endFrame(DspState& _state, const DspFrameContext& _context)
	{
		if (_context.request.executeProgram)
			_state.eramPos = (_state.eramPos - 1) & 0xffff;
		_state.iramSelPhase = !_state.iramSelPhase;
	}
} // namespace xpLib::dspOps

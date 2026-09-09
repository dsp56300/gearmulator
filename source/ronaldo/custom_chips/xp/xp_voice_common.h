#pragma once

// Constants, tables and fixed-point helpers shared by the voice engines and the host interface of the XP.

#include "xp.h"
#include "../pcmInterpolation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace xpLib::xpInternal
{
	using Address = XP::HostAddress;

	constexpr uint16_t voiceWideStride = 4;
	constexpr uint16_t voiceNarrowStride = 2;
	constexpr uint16_t waveBufferEnd = Address::dpcmAccumulator_0c00;
	constexpr uint16_t tvaGainEnd = Address::filterBp_2800;
	constexpr uint16_t cramEnd = 0x2e40;
	constexpr uint16_t iramEnd = Address::iram3Targets_3300;
	constexpr uint16_t iram3TargetsEnd = 0x3380;
	constexpr uint16_t pramEnd = 0x3880;
	constexpr uint16_t mixerEnd = Address::waveRomAperture_3c00;
	constexpr uint16_t waveRomApertureEnd = 0x4000;
	constexpr uint32_t voiceAddressMask = 0x0fffff;
	constexpr uint32_t playbackStarting = 0x10000;
	constexpr uint32_t playbackRunning = 0x30000;
	constexpr uint32_t rampRateMask = 0x00fff;
	constexpr uint32_t rampIntervalMask = 0x03000;
	constexpr uint32_t rampCurveMask = 0x0c000;
	constexpr uint32_t rampTrunkComplete = 0x04000;
	constexpr uint32_t rampEventArm = 0x10000;
	constexpr uint32_t rampHold = 0x20000;
	constexpr uint32_t rampAcknowledge = 1;
	constexpr uint32_t rampFieldMask = 0x3ffff;
	constexpr uint32_t rampScratchMask = 0xfffff;
	constexpr uint32_t addressPhaseMask = 0x3ffff;
	constexpr uint32_t addressPhaseFractionMask = 0x03fff;
	constexpr uint32_t dpcmMask = 0x3ffff;
	constexpr uint32_t filterMask = 0xffffff;
	constexpr uint32_t mixerDestinationMask = 0x003f;
	constexpr unsigned mixerLevelShift = 6;
	constexpr unsigned mixerProductShift = 9;

	// ROM N: three signed-sample interpolation taps at 128 fractional phases
	using pcmInterpolation::interpolationCoefficients;

	// Cache round(0x4000 * 2^(index / 256)) once, outside voice processing.
	inline const std::array<uint16_t, 256> pitchMantissa = []
	{
		std::array<uint16_t, 256> values{};
		for (size_t i = 0; i < values.size(); ++i)
			values[i] = static_cast<uint16_t>(std::lround(0x4000 * std::exp2(static_cast<double>(i) / 256.0)));
		return values;
	}();

	inline uint32_t widthMask(const uint8_t _width) { return (uint32_t{1} << _width) - 1; }

	inline int32_t signExtend(const uint32_t _value, const unsigned _width)
	{
		const auto sign = uint32_t{1} << (_width - 1);
		return static_cast<int32_t>((_value ^ sign) - sign);
	}

	// Arithmetic right shift.
	inline int64_t arithmeticShiftRight(const int64_t _value, const unsigned _shift)
	{
		return _value >> _shift;
	}

	inline int32_t saturateSigned24(const int64_t _value)
	{
		return static_cast<int32_t>(std::clamp<int64_t>(_value, -0x800000, 0x7fffff));
	}

	inline int32_t saturateSigned16(const int64_t _value)
	{
		return static_cast<int32_t>(std::clamp<int64_t>(_value, -0x8000, 0x7fff));
	}

	inline uint32_t decodePitch(const uint32_t _pitch)
	{
		if (_pitch == 0)
			return 0;

		const auto index = static_cast<size_t>((_pitch >> 6) & 0xff);
		const auto fraction = _pitch & 0x3f;
		const auto a = ((64 - fraction) * pitchMantissa[index]) >> 2;
		// The next-octave endpoint is 0x8000; it needs no 257th ROM entry.
		const auto next = index + 1 == pitchMantissa.size() ? 0x8000 : pitchMantissa[index + 1];
		const auto b = (fraction * next) >> 2;
		const auto octaveShift = (~(_pitch >> 14)) & 0x0f;
		return ((a + b) >> 1) >> octaveShift;
	}

	inline uint32_t unpackAddressPhase(const uint32_t _packed)
	{
		return ((_packed & 0x0f) << 14) | ((_packed >> 4) & addressPhaseFractionMask);
	}

	inline uint32_t packAddressPhase(const uint32_t _linear)
	{
		return ((_linear & addressPhaseFractionMask) << 4) | ((_linear >> 14) & 0x0f);
	}

	inline uint8_t reverseTwoBits(const uint8_t _value)
	{
		return static_cast<uint8_t>(((_value & 1) << 1) | ((_value & 2) >> 1));
	}

	inline uint32_t rampDividerMask(const uint32_t _control)
	{
		switch ((_control & rampIntervalMask) >> 12)
		{
		case 0:
			return 0;
		case 1:
			return 7;
		case 2:
			return 31;
		default:
			return 127;
		}
	}

	inline uint32_t calculateLinearRampStep(const uint32_t _current, const uint32_t _target, const uint32_t _control)
	{
		const auto difference = static_cast<int32_t>(_target) - static_cast<int32_t>(_current);
		const auto step = arithmeticShiftRight(
			static_cast<int64_t>(arithmeticShiftRight(difference, 3)) * (_control & rampRateMask), 10);
		return static_cast<uint32_t>(step * 2 + (difference > 0 ? 1 : 0)) & rampScratchMask;
	}

	inline void stepLinearRamp(uint32_t& _current, const uint32_t _target, const uint32_t _scratch, const uint8_t _parity)
	{
		if (_current == _target)
			return;
		const auto ascending = _current < _target;
		const auto step = signExtend(_scratch, 20);
		const auto delta = arithmeticShiftRight(step + (ascending ? _parity : 0), 1);
		auto next = static_cast<int64_t>(_current) + delta;
		if ((ascending && next > _target) || (!ascending && next < _target))
			next = _target;
		_current = static_cast<uint32_t>(next) & rampFieldMask;
	}

	inline void stepLogRamp(uint32_t& _current, const uint32_t _target, const uint32_t _control, const uint32_t _quantum,
					 const uint8_t _parity)
	{
		if (_current == _target)
			return;
		const auto difference = static_cast<int32_t>(_target) - static_cast<int32_t>(_current);
		const auto scaledDifference = difference / static_cast<int32_t>(_quantum);
		const auto step =
			arithmeticShiftRight(
				static_cast<int64_t>(arithmeticShiftRight(scaledDifference, 3)) * (_control & rampRateMask), 10) *
			static_cast<int32_t>(_quantum);
		const auto correction = _quantum > 1 ? static_cast<int32_t>(_quantum >> 1) : static_cast<int32_t>(_parity);
		auto next = static_cast<int64_t>(_current) + step;
		if ((difference > 0 && next < _target) || ((_control & rampRateMask) == 0 && _current != _target))
			next += correction;
		if (next < 0)
			next = 0;
		else if (next > (_quantum > 1 ? rampScratchMask : rampFieldMask))
			next &= _quantum > 1 ? rampScratchMask : rampFieldMask;
		if (_quantum > 1)
			next &= ~int64_t{1};
		_current = static_cast<uint32_t>(next);
	}

	inline uint32_t combineAmp(const XP::VoiceState& _voice)
	{
		if ((_voice.ampModRamp_1900 & 0x04000) != 0)
		{
			const auto sum = static_cast<uint64_t>(_voice.ampCurrent_1e00 + _voice.ampModCurrent_1d00) << 2;
			return static_cast<uint32_t>(std::min<uint64_t>(sum, rampScratchMask & ~uint32_t{1}));
		}
		const auto product =
			(static_cast<uint64_t>(_voice.ampCurrent_1e00 >> 3) * (_voice.ampModCurrent_1d00 >> 4)) >> 8;
		return static_cast<uint32_t>(std::min<uint64_t>(product & ~uint64_t{1}, rampScratchMask & ~uint32_t{1}));
	}
} // namespace xpLib::xpInternal

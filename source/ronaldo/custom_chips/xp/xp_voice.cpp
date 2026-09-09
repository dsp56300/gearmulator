#include "xp.h"
#include "xp_voice_common.h"

// The voice source stage: the chip's eight-cell wave FIFO, exponent cache and packed phase register,
// reproduced as host-visible state. The FIFO doubles as the decoded-sample cache the interpolator reads
// from; reading ROM per tap instead was measured slower.

namespace xpLib
{
	using namespace xpInternal;
	XP::VoiceStepResult XP::stepVoiceSource(const size_t _voiceIndex, VoiceState& _voice)
	{
		raiseVoiceMuteEvent(_voiceIndex, _voice);

		if (_voice.runtimeCache.runtimePhase == VoiceRuntimePhaseCache::preload)
		{
			if ((_voice.waveControl_0000 & WaveControl::ldpcmFormat) != 0)
				_voice.runtimeCache.waveSampleFormat = WaveSampleFormatCache::ldpcm;
			else if ((_voice.waveControl_0000 & WaveControl::fceDpcmExponent0OrProducerLive) != 0)
				_voice.runtimeCache.waveSampleFormat = WaveSampleFormatCache::fceDpcmExponent0;
			else
				_voice.runtimeCache.waveSampleFormat = WaveSampleFormatCache::fceDpcm;

			// TODO: model wave-ROM descriptor bit 1, 16-bit fetches from a physical 8-bit ROM,
			// launches with a nonzero playbackStateConfig_1000 counter (bits 0-2), and
			// ldpcmProducerModifier. These modes currently follow the ordinary preload path.
			if ((_voice.waveControl_0000 & WaveControl::waveEngineHalt) == 0)
			{
				const auto reverse = voiceAddressRunsReverse(_voice);
				// TODO: reverse preload on an 8-bit wave bus is not modeled.
				auto preloadCount = _voice.waveCircularBuffer_0800.size() - 1;
				if (voiceUses16BitWaveBus(_voice))
				{
					preloadCount = reverse ? 7 + (_voice.sampleCurrent_0100 & 1)
										   : _voice.waveCircularBuffer_0800.size() - (_voice.sampleCurrent_0100 & 1);
					const auto consumer = static_cast<size_t>(_voice.addressFraction_0e00 & 0x0f);
					if (consumer != 0)
					{
						const auto available = (consumer & 8) == 0 ? 0
							: reverse ? ((consumer - 9) | 1) + (_voice.sampleCurrent_0100 & 1)
									  : consumer & 6;
						preloadCount = std::min(preloadCount, available);
					}
				}

				if (preloadCount == 0)
				{
					// Consumer positions 1-9 leave no producer slots. The chip preserves 0x0400 and marks the
					// wave pipeline blocked until the next launch.
					_voice.waveControl_0000 |= WaveControl::producerBlocked;
				}
				else
				{
					// One or two exponent-byte pairs plus at most eight mantissa reads fit the 12-cycle voice slot.
					_voice.waveFetchState_0400 = readExponentCache(_voice, _voice.sampleCurrent_0100);
					for (size_t i = 0; i < preloadCount; ++i)
					{
						_voice.waveCircularBuffer_0800[i] = readVoiceSampleFromCache(_voice, _voice.sampleCurrent_0100);
						const auto looped = advanceVoiceAddress(_voiceIndex, _voice);
						refreshExponentCacheAfterAdvance(_voice, looped);
					}
					_voice.waveFetchState_0400 =
						(static_cast<uint32_t>(preloadCount) << 16) | (_voice.waveFetchState_0400 & 0xffff);
					// The chip overlays this marker only after the producer has completed its preload.
					_voice.waveControl_0000 |= WaveControl::fceDpcmExponent0OrProducerLive;
				}
			}

			_voice.runtimeCache.runtimePhase = VoiceRuntimePhaseCache::initialize;
			return {};
		}

		if (_voice.runtimeCache.runtimePhase == VoiceRuntimePhaseCache::initialize)
		{
			const auto pitch = _voice.pitchCurrent_1b00;
			const auto tvfF = _voice.tvfFCurrent_1c00;
			if (_voice.pitchRamp_1700 == 0x04005 && _voice.pitchDestination_1200 == _voice.pitchCurrent_1b00)
				_voice.pitchCurrent_1b00 &= ~rampAcknowledge;
			if (_voice.tvfFRamp_1800 == 0x04005 && _voice.tvfFDestination_1300 == _voice.tvfFCurrent_1c00)
				_voice.tvfFCurrent_1c00 &= ~rampAcknowledge;
			if (_voice.ampRamp_1a00 == 0x04005 && _voice.ampDestination_1500 == _voice.ampCurrent_1e00)
				_voice.ampCurrent_1e00 &= ~rampAcknowledge;
			_voice.pitchIncrement_0d00 = decodePitch(pitch);
			_voice.tvfFCoefficient_2200 = (decodePitch(tvfF) << 2) & rampScratchMask;
			const auto pitchTarget = _voice.pitchDestination_1200 & ~rampAcknowledge;
			if ((_voice.pitchRamp_1700 & rampHold) == 0 &&
				((m_state.sampleClock >> 3) & rampDividerMask(_voice.pitchRamp_1700)) == 0 &&
				_voice.pitchCurrent_1b00 != pitchTarget)
			{
				const auto curve = _voice.pitchRamp_1700 & rampCurveMask;
				const auto parity = static_cast<uint8_t>(((m_state.sampleClock >> 3) ^ 1) & 1);
				if (curve == 0x04000)
				{
					const auto step =
						calculateLinearRampStep(_voice.pitchCurrent_1b00, pitchTarget, _voice.pitchRamp_1700);
					stepLinearRamp(_voice.pitchCurrent_1b00, pitchTarget, step, parity);
				}
				else
					stepLogRamp(_voice.pitchCurrent_1b00, pitchTarget, _voice.pitchRamp_1700, 1, parity);
			}
			_voice.playbackStateConfig_1000 = playbackStarting | (_voice.playbackStateConfig_1000 & 0x1f);
			_voice.runtimeCache.runtimePhase = VoiceRuntimePhaseCache::starting;
			return {};
		}

		if (_voice.runtimeCache.runtimePhase == VoiceRuntimePhaseCache::starting)
		{
			const auto target = _voice.tvfFDestination_1300 & ~rampAcknowledge;
			if ((_voice.tvfFRamp_1800 & rampHold) == 0 &&
				((m_state.sampleClock >> 3) & rampDividerMask(_voice.tvfFRamp_1800)) == 0 &&
				_voice.tvfFCurrent_1c00 != target)
			{
				const auto curve = _voice.tvfFRamp_1800 & rampCurveMask;
				const auto parity = static_cast<uint8_t>(((m_state.sampleClock >> 3) ^ 1) & 1);
				if (curve == 0x04000)
				{
					const auto step = calculateLinearRampStep(_voice.tvfFCurrent_1c00, target, _voice.tvfFRamp_1800);
					stepLinearRamp(_voice.tvfFCurrent_1c00, target, step, parity);
				}
				else
					stepLogRamp(_voice.tvfFCurrent_1c00, target, _voice.tvfFRamp_1800, 1, parity);
			}
			_voice.playbackStateConfig_1000 = playbackRunning | (_voice.playbackStateConfig_1000 & 0x1f);
			_voice.runtimeCache.runtimePhase = VoiceRuntimePhaseCache::running;
			return {};
		}

		if (_voice.runtimeCache.runtimePhase != VoiceRuntimePhaseCache::running)
			return {};

		const auto playbackCounter = (_voice.playbackStateConfig_1000 + 1) & 7;
		_voice.playbackStateConfig_1000 = playbackRunning | (_voice.playbackStateConfig_1000 & 0x18) | playbackCounter;
		if ((_voice.waveControl_0000 & (WaveControl::waveEngineHalt | WaveControl::producerBlocked)) != 0)
		{
			if (playbackCounter == 6)
			{
				_voice.pitchIncrement_0d00 = decodePitch(_voice.pitchCurrent_1b00);
				_voice.tvfFCoefficient_2200 = (decodePitch(_voice.tvfFCurrent_1c00) << 2) & rampScratchMask;
			}
			stepVoiceRamps(_voiceIndex, _voice);
			return {};
		}

		const auto phase = unpackAddressPhase(_voice.addressFraction_0e00);
		// RAM M sees three interpolation reads, at most four DPCM reads, then at most four producer writes.
		const auto interpolatedSample = interpolateVoice(_voice, phase);
		// The Q16 bits below readable 0x0e00 are ordered-dithered by the chip-global four-frame phase. The threshold
		// order is 0,2,1,3 (two-bit reversal), applied to the sample-clock phase shifted by two.
		const auto ditherPhase = static_cast<uint8_t>((m_state.sampleClock + 3) & 3);
		const auto phaseIncrement =
			(_voice.pitchIncrement_0d00 >> 2) + ((_voice.pitchIncrement_0d00 & 3) > reverseTwoBits(ditherPhase));
		const auto nextPhase = phase + phaseIncrement;
		_voice.addressFraction_0e00 = packAddressPhase(nextPhase & addressPhaseMask);

		const auto cellCount = (nextPhase >> 14) - (phase >> 14);
		const auto consumer = static_cast<size_t>(phase >> 14);
		for (uint32_t cell = 0; cell < cellCount; ++cell)
		{
			const auto delta = decodeVoiceDelta(_voice, _voice.waveCircularBuffer_0800[(consumer + cell) & 7]);
			_voice.dpcmAccumulator_0c00 = (_voice.dpcmAccumulator_0c00 + static_cast<uint32_t>(delta)) & dpcmMask;
		}

		if (voiceUses16BitWaveBus(_voice))
		{
			const auto pairAlignment = (_voice.waveFetchState_0400 >> 16) & 1;
			const auto phaseOffset = pairAlignment << 14;
			const auto pairCount = ((nextPhase + phaseOffset) >> 15) - ((phase + phaseOffset) >> 15);
			for (uint32_t pair = 0; pair < pairCount; ++pair)
				refillVoicePair(_voiceIndex, _voice);
		}
		else
		{
			for (uint32_t cell = 0; cell < cellCount; ++cell)
				refillVoiceCell(_voiceIndex, _voice);
		}
		if (playbackCounter == 6)
		{
			_voice.pitchIncrement_0d00 = decodePitch(_voice.pitchCurrent_1b00);
			_voice.tvfFCoefficient_2200 = (decodePitch(_voice.tvfFCurrent_1c00) << 2) & rampScratchMask;
		}
		stepVoiceRamps(_voiceIndex, _voice);
		return {interpolatedSample, true};
	}

	bool XP::voiceAddressRunsReverse(const VoiceState& _voice) const
	{
		return ((_voice.waveControl_0000 & WaveControl::reverse) != 0) !=
			((_voice.waveControl_0000 & WaveControl::directionState) != 0);
	}

	bool XP::voiceUses16BitWaveBus(const VoiceState& _voice) const
	{
		const auto chipSelect = static_cast<size_t>((_voice.waveControl_0000 >> 4) & 7);
		return (m_state.waveRomConfig[chipSelect] & 1) != 0;
	}

	void XP::refillVoiceCell(const size_t _voiceIndex, VoiceState& _voice)
	{
		const auto pointer = static_cast<size_t>(_voice.waveFetchState_0400 >> 16) & 0x0f;
		const auto cell = pointer & 7;
		_voice.waveCircularBuffer_0800[cell] = readVoiceSampleFromCache(_voice, _voice.sampleCurrent_0100);
		const auto looped = advanceVoiceAddress(_voiceIndex, _voice);
		refreshExponentCacheAfterAdvance(_voice, looped);

		const auto nextPointer = static_cast<uint32_t>((pointer + 1) & 0x0f);
		_voice.waveFetchState_0400 = (nextPointer << 16) | (_voice.waveFetchState_0400 & 0xffff);
	}

	void XP::refillVoicePair(const size_t _voiceIndex, VoiceState& _voice)
	{
		const auto pointer = static_cast<size_t>(_voice.waveFetchState_0400 >> 16) & 0x0f;
		const auto cell = pointer & 7;
		_voice.waveCircularBuffer_0800[cell] = readVoiceSampleFromCache(_voice, _voice.sampleCurrent_0100);
		auto looped = advanceVoiceAddress(_voiceIndex, _voice);
		refreshExponentCacheAfterAdvance(_voice, looped);
		_voice.waveCircularBuffer_0800[(cell + 1) & 7] = readVoiceSampleFromCache(_voice, _voice.sampleCurrent_0100);
		looped = advanceVoiceAddress(_voiceIndex, _voice);
		refreshExponentCacheAfterAdvance(_voice, looped);

		const auto nextPointer = static_cast<uint32_t>((pointer + 2) & 0x0f);
		_voice.waveFetchState_0400 = (nextPointer << 16) | (_voice.waveFetchState_0400 & 0xffff);
	}

	uint8_t XP::readVoiceWaveByte(const VoiceState& _voice, const uint32_t _address) const
	{
		const auto chipSelect = static_cast<size_t>((_voice.waveControl_0000 >> 4) & 7);
		const auto& rom = m_waveRoms[chipSelect];
		const auto bank = static_cast<size_t>(_voice.waveControl_0000 & 0x0f) >> rom.voiceBankShift;
		auto voiceAddress = static_cast<size_t>(_address) & voiceAddressMask;
		if (!voiceUses16BitWaveBus(_voice) && rom.width == PhysicalWaveRomWidth::bits16)
			voiceAddress &= ~size_t{1};
		const auto address = (bank << 20) | voiceAddress;
		if (rom.data == nullptr || address >= rom.size)
			return 0;
		return rom.data[address];
	}

	uint16_t XP::readVoiceSampleFromCache(const VoiceState& _voice, const uint32_t _address) const
	{
		const auto exponentIndex = static_cast<unsigned>((_address >> 4) & 3);
		const auto exponent = (_voice.waveFetchState_0400 >> (exponentIndex * 4)) & 0x0f;
		return encodeVoiceSample(_voice, readVoiceWaveByte(_voice, _address), static_cast<uint8_t>(exponent));
	}

	uint16_t XP::encodeVoiceSample(const VoiceState& _voice, const uint8_t _mantissa, const uint8_t _exponent) const
	{
		if (_voice.runtimeCache.waveSampleFormat != WaveSampleFormatCache::fceDpcm)
			return _mantissa;
		return static_cast<uint16_t>((_exponent << 8) | _mantissa);
	}

	int32_t XP::decodeVoiceDelta(const VoiceState& _voice, const uint16_t _encoded) const
	{
		if (_voice.runtimeCache.waveSampleFormat == WaveSampleFormatCache::ldpcm)
		{
			const auto code = static_cast<int32_t>(static_cast<int8_t>(_encoded));
			const auto magnitude = static_cast<uint32_t>(code < 0 ? -code : code);
			const auto shift = magnitude >> 4;
			const auto expanded = shift == 0 ? magnitude & 0x0f : ((magnitude & 0x0f) + 0x10) << (shift - 1);
			return static_cast<int32_t>(expanded << 6) * (code < 0 ? -1 : 1);
		}

		const auto mantissa = static_cast<int32_t>(static_cast<int8_t>(_encoded));
		auto exponent = static_cast<unsigned>((_encoded >> 8) & 0x0f);
		if (_voice.runtimeCache.waveSampleFormat == WaveSampleFormatCache::fceDpcmExponent0)
			exponent = 0;
		else if (exponent > 10)
		{
			// TODO: reserved FCE-DPCM exponents 11-15 are not modeled; use a zero delta.
			return 0;
		}
		return mantissa * static_cast<int32_t>(uint32_t{1} << exponent);
	}

	int32_t XP::interpolateVoice(const VoiceState& _voice, const uint32_t _phase) const
	{
		const auto ratio = static_cast<size_t>((_phase & addressPhaseFractionMask) >> 7);
		const auto consumer = static_cast<size_t>(_phase >> 14);
		auto extended = static_cast<int64_t>(signExtend(_voice.dpcmAccumulator_0c00, 18)) * 4;
		// The sample format is fixed for the voice's whole run, so the per-tap work is selected once.
		switch (_voice.runtimeCache.waveSampleFormat)
		{
		case WaveSampleFormatCache::ldpcm:
			for (size_t tap = 0; tap < interpolationCoefficients.size(); ++tap)
			{
				const auto delta = decodeVoiceDelta(_voice, _voice.waveCircularBuffer_0800[(consumer + tap) & 7]);
				const auto product =
					arithmeticShiftRight(static_cast<int64_t>(interpolationCoefficients[tap][ratio]) * delta, 8);
				extended += arithmeticShiftRight(product * 4, 4);
			}
			break;
		case WaveSampleFormatCache::fceDpcmExponent0:
			for (size_t tap = 0; tap < interpolationCoefficients.size(); ++tap)
			{
				const auto mantissa =
					static_cast<int32_t>(static_cast<int8_t>(_voice.waveCircularBuffer_0800[(consumer + tap) & 7]));
				const auto product = arithmeticShiftRight(
					static_cast<int64_t>(interpolationCoefficients[tap][ratio]) * mantissa * 64, 8);
				extended += arithmeticShiftRight(product * 4, 10);
			}
			break;
		default:
			for (size_t tap = 0; tap < interpolationCoefficients.size(); ++tap)
			{
				const auto encoded = _voice.waveCircularBuffer_0800[(consumer + tap) & 7];
				const auto mantissa = static_cast<int32_t>(static_cast<int8_t>(encoded));
				const auto exponent = static_cast<unsigned>((encoded >> 8) & 0x0f);
				// Reserved FCE-DPCM exponents 11-15 are likewise omitted from interpolation.
				if (exponent <= 10)
				{
					const auto product = arithmeticShiftRight(
						static_cast<int64_t>(interpolationCoefficients[tap][ratio]) * mantissa * 64, 8);
					extended += arithmeticShiftRight(product * 4, 10 - exponent);
				}
			}
			break;
		}

		const auto gain = static_cast<unsigned>((_voice.playbackStateConfig_1000 >> 3) & 3);
		const auto wrapped = signExtend(static_cast<uint32_t>(extended) & 0xfffff, 20);
		return static_cast<int32_t>(arithmeticShiftRight(wrapped, 3 - gain));
	}

	uint16_t XP::readExponentCache(const VoiceState& _voice, const uint32_t _address) const
	{
		if (_voice.runtimeCache.waveSampleFormat != WaveSampleFormatCache::fceDpcm)
			return 0;

		const auto address = (_address & ~uint32_t{0x3f}) >> 5;
		const auto low = readVoiceWaveByte(_voice, address);
		const auto high = readVoiceWaveByte(_voice, address + 1);
		return static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8));
	}

	void XP::refreshExponentCacheAfterAdvance(VoiceState& _voice, const bool _looped)
	{
		if (!_looped)
		{
			const auto boundary = voiceAddressRunsReverse(_voice) ? 0x3f : 0;
			if ((_voice.sampleCurrent_0100 & 0x3f) != boundary)
				return;
		}
		_voice.waveFetchState_0400 =
			(_voice.waveFetchState_0400 & 0x0f0000) | readExponentCache(_voice, _voice.sampleCurrent_0100);
	}

	bool XP::advanceVoiceAddress(const size_t _voiceIndex, VoiceState& _voice)
	{
		const auto reverse = voiceAddressRunsReverse(_voice);
		const auto passedLoopMarker = reverse ? _voice.sampleCurrent_0100 <= _voice.sampleLoop_0200
											  : _voice.sampleCurrent_0100 >= _voice.sampleLoop_0200;
		if (passedLoopMarker)
			raiseVoiceLoopMarkerEvent(_voiceIndex, _voice);

		const auto directionState = (_voice.waveControl_0000 & WaveControl::directionState) != 0;
		const auto boundary = directionState ? _voice.sampleLoop_0200 : _voice.sampleEnd_0300;
		if ((_voice.sampleCurrent_0100 & voiceAddressMask) == (boundary & voiceAddressMask))
		{
			if ((_voice.waveControl_0000 & WaveControl::alternateLoop) != 0)
			{
				_voice.waveControl_0000 ^= WaveControl::directionState;
				return false;
			}

			if ((_voice.sampleLoop_0200 & voiceAddressMask) == (_voice.sampleEnd_0300 & voiceAddressMask))
			{
				return false;
			}

			_voice.sampleCurrent_0100 =
				(directionState ? _voice.sampleEnd_0300 : _voice.sampleLoop_0200) & voiceAddressMask;
			return true;
		}

		if (reverse)
			_voice.sampleCurrent_0100 = (_voice.sampleCurrent_0100 - 1) & voiceAddressMask;
		else
			_voice.sampleCurrent_0100 = (_voice.sampleCurrent_0100 + 1) & voiceAddressMask;
		return false;
	}
} // namespace xpLib

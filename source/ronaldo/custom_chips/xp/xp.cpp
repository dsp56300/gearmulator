//
// XP Chip Emulator
// Many thanks to NukeYKT for figuring out the GP chip, which was essential
// to figure out the PCM playback engine and the sample format.
//

#include "xp.h"
#include "xp_voice_common.h"

#include <algorithm>

namespace xpLib
{
	using namespace xpInternal;

	XP::XP() = default;

	void XP::reset()
	{
		const bool interruptWasAsserted = m_interruptLine;
		m_state = State{};
		m_hostWriteBytes.fill(0);
		m_hostReadWord = 0;
		m_hostReadAddress = 0xffff;
		m_interruptLine = false;
		m_irqEventAcceptedThisStep = false;
		if (interruptWasAsserted && m_interruptCallback)
			m_interruptCallback(false);
	}

	void XP::prepareSample()
	{
		// Boundary model: one call commits a complete sample frame. Each enabled voice contributes four
		// three-master-clock DSP slots; the production 64-voice setting is therefore 768 master clocks.
		// The mixer consumes the preceding frame's 0x2a00 values before each voice slot overwrites them.
		m_irqEventAcceptedThisStep = false;
		m_state.irqBlockedEvent = false;
		const auto voiceCount = std::min<size_t>((m_state.highestVoice & 0x3f) + 1, nMaxVoices);
		m_frameVoices = voiceCount;
		prepareMixer(voiceCount);
		for (size_t voiceIndex = 0; voiceIndex < voiceCount; ++voiceIndex)
		{
			auto& voice = m_state.voices[voiceIndex];
			if (!voice.resetState_3900.released && voice.tvaGain_2700 != 0)
				voice.tvaGain_2700 = static_cast<uint16_t>(std::max<uint32_t>(1, (voice.tvaGain_2700 * 7u) >> 3));
		}
		const auto updateTvaBeforeAudio = [this](VoiceState& _voice)
		{
			const auto phase = _voice.runtimeCache.runtimePhase;
			if (_voice.resetState_3900.released &&
				(phase == VoiceRuntimePhaseCache::starting || phase == VoiceRuntimePhaseCache::running))
				stepTvaGain(_voice);
		};

		for (size_t voiceIndex = 0; voiceIndex < voiceCount; ++voiceIndex)
		{
			auto& voice = m_state.voices[voiceIndex];
			const auto structure = static_cast<unsigned>((voice.filterConfig_2000 & FilterConfig::structureMask) >> 12);
			const auto paired = (voiceIndex & 1) == 0 && voiceIndex + 1 < voiceCount &&
				(voice.filterConfig_2000 & FilterConfig::pairRole) != 0 && structure >= 1 && structure <= 9;
			if (paired)
			{
				auto& partner = m_state.voices[voiceIndex + 1];
				VoiceStepResult ownerStep{};
				VoiceStepResult partnerStep{};
				if (voice.resetState_3900.released)
				{
					updateTvaBeforeAudio(voice);
					ownerStep = stepVoiceSource(voiceIndex, voice);
				}
				else
					voice.filterBp_2800 = voice.filterLp_2900 = voice.filterOutput_2a00 = 0;
				if (partner.resetState_3900.released)
				{
					updateTvaBeforeAudio(partner);
					partnerStep = stepVoiceSource(voiceIndex + 1, partner);
				}
				else
					partner.filterBp_2800 = partner.filterLp_2900 = partner.filterOutput_2a00 = 0;
				if (ownerStep.filterDue || partnerStep.filterDue)
					stepPairedVoiceFilter(voice, partner, ownerStep.filterDue ? ownerStep.sample : 0,
										  partnerStep.filterDue ? partnerStep.sample : 0);
				++voiceIndex;
			}
			else if (voice.resetState_3900.released)
			{
				updateTvaBeforeAudio(voice);
				const auto result = stepVoiceSource(voiceIndex, voice);
				if (result.filterDue)
					voice.filterOutput_2a00 =
						static_cast<uint32_t>(
							applyTva(voice,
									 stepVoiceFilter(voice, result.sample,
													 static_cast<unsigned>(
														 (voice.filterConfig_2000 & FilterConfig::modeMask) >> 10)))) &
						filterMask;
			}
			else
			{
				voice.filterBp_2800 = 0;
				voice.filterLp_2900 = 0;
				voice.filterOutput_2a00 = 0;
			}
		}

		const auto dspRunning = (m_state.dspControl & 4) != 0;
		if (dspRunning && (m_state.sampleClock & 1) != 0)
			m_state.dsp.stepIram3Ramps(m_state.iram3RampRates, Dsp::iram3PartitionCount(m_state.serialAudioConfig[1]),
									   static_cast<uint8_t>(m_state.sampleClock >> 1));
	}

	Dsp::StepRequest XP::dspStepRequest() const
	{
		const auto serialInputRunning = (m_state.dspControl & 2) != 0;
		return {(m_state.dspControl & 4) != 0,
				serialInputRunning,
				m_state.serialAudioConfig[0],
				m_state.serialAudioConfig[1],
				(m_state.dspControl & 3) == 3,
				m_frameVoices * 4,
				&m_frame};
	}

	void XP::finishSample()
	{
		++m_state.sampleClock;
		updateInterruptLine();
	}

	void XP::step()
	{
		prepareSample();
		auto request = dspStepRequest();
		m_state.dsp.hoistDeposits(request, m_mixerSummary);
		m_state.dsp.step(request);
		finishSample();
	}

	void XP::stepLinked(XP& _a, XP& _b)
	{
		_a.prepareSample();
		_b.prepareSample();
		auto aRequest = _a.dspStepRequest();
		auto bRequest = _b.dspStepRequest();
		_a.m_state.dsp.hoistDeposits(aRequest, _a.m_mixerSummary, true);
		_b.m_state.dsp.hoistDeposits(bRequest, _b.m_mixerSummary, true);
		Dsp::stepLinked(_a.m_state.dsp, aRequest, _b.m_state.dsp, bRequest);
		_a.finishSample();
		_b.finishSample();
	}

	void XP::updateInterruptLine()
	{
		const auto level = m_state.interrupt;
		if (level == m_interruptLine)
			return;
		m_interruptLine = level;
		if (m_interruptCallback)
			m_interruptCallback(level);
	}

	void XP::stepVoiceRamps(const size_t _voiceIndex, VoiceState& _voice)
	{
		const auto playbackCounter = static_cast<uint8_t>(_voice.playbackStateConfig_1000 & 7);
		const auto slowCounter = m_state.sampleClock >> 3;
		const auto ampCounter = m_state.sampleClock >> 1;
		const auto tickDue = [](const uint32_t _control, const uint64_t _counter)
		{ return (_counter & rampDividerMask(_control)) == 0; };
		const auto destinationPending = [](const uint32_t _destination)
		{ return (_destination & rampAcknowledge) == 0; };

		// The visible playback counter selects the voice's one envelope-RAM operation: amp on odd counters, with
		// amp-mod, TVF-Q, TVF-F, and pitch on even counters 0, 2, 4, and 6 respectively.
		if ((playbackCounter & 1) == 0)
		{
			switch (playbackCounter)
			{
			case 6: // Pitch
				{
					const auto pending = destinationPending(_voice.pitchDestination_1200);
					const auto target = _voice.pitchDestination_1200 & ~rampAcknowledge;
					if (pending || _voice.pitchCurrent_1b00 != target)
						_voice.pitchIncrement_0d00 = decodePitch(_voice.pitchCurrent_1b00);
					const auto linear = (_voice.pitchRamp_1700 & rampCurveMask) == 0x04000;
					if (pending)
					{
						_voice.pitchDestination_1200 |= rampAcknowledge;
						if (linear)
						{
							_voice.pitchStep_2400 =
								calculateLinearRampStep(_voice.pitchCurrent_1b00, target, _voice.pitchRamp_1700);
							break;
						}
					}
					if ((_voice.pitchRamp_1700 & rampHold) == 0 && tickDue(_voice.pitchRamp_1700, slowCounter))
					{
						const auto parity = static_cast<uint8_t>((slowCounter ^ 1) & 1);
						if (linear)
							stepLinearRamp(_voice.pitchCurrent_1b00, target, _voice.pitchStep_2400, parity);
						else
							stepLogRamp(_voice.pitchCurrent_1b00, target, _voice.pitchRamp_1700, 1, parity);
						if (_voice.pitchCurrent_1b00 == target)
							raiseVoiceRampTerminalEvent(_voiceIndex, _voice.pitchRamp_1700, IrqReason::pitchTerminal);
					}
					break;
				}
			case 0: // Amp modulation
				{
					const auto target = _voice.ampModDestination_1400 & ~rampAcknowledge;
					if (destinationPending(_voice.ampModDestination_1400))
						_voice.ampModDestination_1400 |= rampAcknowledge;
					if ((_voice.ampModRamp_1900 & rampHold) == 0 && tickDue(_voice.ampModRamp_1900, slowCounter))
					{
						stepLogRamp(_voice.ampModCurrent_1d00, target, _voice.ampModRamp_1900, 1,
									static_cast<uint8_t>(slowCounter & 1));
						if (_voice.ampModCurrent_1d00 == target)
							raiseVoiceRampTerminalEvent(_voiceIndex, _voice.ampModRamp_1900, IrqReason::ampModTerminal);
					}
					break;
				}
			case 2: // TVF-Q
				{
					const auto target = ((_voice.tvfQDestination_1100 & ~rampAcknowledge) << 2) & rampScratchMask;
					if (destinationPending(_voice.tvfQDestination_1100))
						_voice.tvfQDestination_1100 |= rampAcknowledge;
					if ((_voice.tvfQRamp_1600 & rampHold) == 0 && tickDue(_voice.tvfQRamp_1600, slowCounter))
					{
						stepLogRamp(_voice.tvfQCurrent_2100, target, _voice.tvfQRamp_1600, 4, 0);
						if (_voice.tvfQCurrent_2100 == target)
							raiseVoiceRampTerminalEvent(_voiceIndex, _voice.tvfQRamp_1600, IrqReason::tvfQTerminal);
					}
					break;
				}
			case 4: // TVF-F
				{
					const auto pending = destinationPending(_voice.tvfFDestination_1300);
					const auto target = _voice.tvfFDestination_1300 & ~rampAcknowledge;
					if (pending || _voice.tvfFCurrent_1c00 != target)
						_voice.tvfFCoefficient_2200 = (decodePitch(_voice.tvfFCurrent_1c00) << 2) & rampScratchMask;
					const auto linear = (_voice.tvfFRamp_1800 & rampCurveMask) == 0x04000;
					if (pending)
					{
						_voice.tvfFDestination_1300 |= rampAcknowledge;
						if (linear)
						{
							_voice.tvfFStep_2500 =
								calculateLinearRampStep(_voice.tvfFCurrent_1c00, target, _voice.tvfFRamp_1800);
							break;
						}
					}
					if ((_voice.tvfFRamp_1800 & rampHold) == 0 && tickDue(_voice.tvfFRamp_1800, slowCounter))
					{
						const auto parity = static_cast<uint8_t>((slowCounter ^ 1) & 1);
						if (linear)
							stepLinearRamp(_voice.tvfFCurrent_1c00, target, _voice.tvfFStep_2500, parity);
						else
							stepLogRamp(_voice.tvfFCurrent_1c00, target, _voice.tvfFRamp_1800, 1, parity);
						if (_voice.tvfFCurrent_1c00 == target)
							raiseVoiceRampTerminalEvent(_voiceIndex, _voice.tvfFRamp_1800, IrqReason::tvfFTerminal);
					}
					break;
				}
			default:
				break;
			}
			return;
		}

		// The amp slot publishes the gain from the incoming currents, then advances amp. Amp-mod therefore becomes
		// visible in 0x2300 on the following sample, and amp itself has one amp-slot (two-sample) latency there.
		_voice.combinedAmp_2300 = combineAmp(_voice);
		const auto parity = static_cast<uint8_t>((playbackCounter >> 1) & 1);

		const auto curve = static_cast<uint8_t>((_voice.ampRamp_1a00 & rampCurveMask) >> 14);
		const auto pending = destinationPending(_voice.ampDestination_1500);
		if (curve == 2 && (_voice.runtimeCache.ampCurve2EntryPending || pending))
		{
			_voice.ampDestination_1500 = (_voice.ampCurrent_1e00 >> 1) | rampAcknowledge;
			_voice.ampStep_2600 = 0;
			_voice.runtimeCache.ampCurve2EntryPending = false;
			return;
		}

		const auto target = _voice.ampDestination_1500 & ~rampAcknowledge;
		if (pending && curve != 3)
		{
			_voice.ampDestination_1500 |= rampAcknowledge;
			if (curve == 1)
			{
				_voice.ampStep_2600 = calculateLinearRampStep(_voice.ampCurrent_1e00, target, _voice.ampRamp_1a00);
				return;
			}
		}
		if ((_voice.ampRamp_1a00 & rampHold) != 0 || !tickDue(_voice.ampRamp_1a00, ampCounter))
			return;

		if (curve == 0)
		{
			stepLogRamp(_voice.ampCurrent_1e00, target, _voice.ampRamp_1a00, 1, parity);
			// Ordinary amp curves terminate at any programmed target. Trunk curves retain their zero-boundary behavior.
			if (_voice.ampCurrent_1e00 == target)
				raiseVoiceRampTerminalEvent(_voiceIndex, _voice.ampRamp_1a00, IrqReason::ampTerminal);
			return;
		}
		if (curve == 1)
		{
			stepLinearRamp(_voice.ampCurrent_1e00, target, _voice.ampStep_2600, parity);
			if (_voice.ampCurrent_1e00 == target)
				raiseVoiceRampTerminalEvent(_voiceIndex, _voice.ampRamp_1a00, IrqReason::ampTerminal);
			return;
		}

		const auto previous = _voice.ampCurrent_1e00;
		const auto accumulator = signExtend(_voice.ampStep_2600, 20);
		auto next = static_cast<int64_t>(_voice.ampCurrent_1e00) + arithmeticShiftRight(accumulator + parity, 1);
		if (next < 0 || next > rampFieldMask)
			next = 0;
		_voice.ampCurrent_1e00 = static_cast<uint32_t>(next);
		if (curve == 2)
		{
			const auto threshold = _voice.ampDestination_1500 & ~rampAcknowledge;
			const auto delta = previous > threshold ? -static_cast<int32_t>(_voice.ampRamp_1a00 & rampRateMask)
													: static_cast<int32_t>(_voice.ampRamp_1a00 & rampRateMask);
			_voice.ampStep_2600 = static_cast<uint32_t>(accumulator + delta) & rampScratchMask;
			if (previous != 0 && _voice.ampCurrent_1e00 == 0)
			{
				_voice.ampRamp_1a00 |= rampTrunkComplete;
				raiseVoiceRampTerminalEvent(_voiceIndex, _voice.ampRamp_1a00, IrqReason::ampTerminal);
			}
			return;
		}

		const auto destination = _voice.ampDestination_1500 & ~rampAcknowledge;
		if (previous == 0)
		{
			// Curve 3 treats the zero boundary before its ordinary steering
			// update. Negative velocity is decelerated by the programmed rate;
			// zero or positive velocity is replaced by the -1 sentinel.
			_voice.ampStep_2600 = accumulator < 0
				? static_cast<uint32_t>(accumulator + static_cast<int32_t>(_voice.ampRamp_1a00 & rampRateMask)) &
					rampScratchMask
				: rampScratchMask;
			raiseVoiceRampTerminalEvent(_voiceIndex, _voice.ampRamp_1a00, IrqReason::ampTerminal);
			return;
		}
		if (_voice.ampCurrent_1e00 == 0)
		{
			// A nonzero-to-zero crossing reports the incoming scratch value;
			// the ordinary steering delta is not applied on the terminal service.
			raiseVoiceRampTerminalEvent(_voiceIndex, _voice.ampRamp_1a00, IrqReason::ampTerminal);
			return;
		}
		const auto delta = previous > destination ? -static_cast<int32_t>(_voice.ampRamp_1a00 & rampRateMask)
												  : static_cast<int32_t>(_voice.ampRamp_1a00 & rampRateMask);
		_voice.ampStep_2600 = static_cast<uint32_t>(accumulator + delta) & rampScratchMask;
	}

	int32_t XP::stepVoiceFilter(VoiceState& _voice, const int32_t _input, const unsigned _mode)
	{
		const auto oldBp = signExtend(_voice.filterBp_2800, 24);
		const auto oldLp = signExtend(_voice.filterLp_2900, 24);
		const auto frequency = _voice.tvfFCoefficient_2200 & rampScratchMask;
		const auto damping = _voice.tvfQCurrent_2100 & rampScratchMask;

		// Both coefficients are unsigned Q19. Each of the Chamberlin operations saturates independently in the
		// signed 24-bit state domain. The coefficient products truncate toward zero; this differs from an arithmetic
		// right shift by one LSB for negative products with discarded fractional bits.
		const auto multiplyCoefficient = [](const int32_t _value, const uint32_t _coefficient)
		{ return (static_cast<int64_t>(_value) * _coefficient) / (int64_t{1} << 19); };
		const auto nextLp = saturateSigned24(static_cast<int64_t>(oldLp) + multiplyCoefficient(oldBp, frequency));
		const auto highPass =
			saturateSigned24(static_cast<int64_t>(_input) - nextLp - multiplyCoefficient(oldBp, damping));
		const auto nextBp = saturateSigned24(static_cast<int64_t>(oldBp) + multiplyCoefficient(highPass, frequency));
		_voice.filterLp_2900 = static_cast<uint32_t>(nextLp) & filterMask;
		_voice.filterBp_2800 = static_cast<uint32_t>(nextBp) & filterMask;

		int32_t selected = 0;
		switch (_mode & 3)
		{
		case 0:
			selected = nextLp;
			break;
		case 1:
			selected = nextBp;
			break;
		case 2:
			selected = highPass;
			break;
		default:
			selected = saturateSigned24(static_cast<int64_t>(highPass) - nextLp);
			break;
		}
		return selected;
	}

	int32_t XP::applyTva(const VoiceState& _voice, const int32_t _input) const
	{
		const auto gain = static_cast<uint32_t>(_voice.tvaGain_2700) << 4;
		return saturateSigned24((static_cast<int64_t>(_input) * gain) / (int64_t{1} << 19));
	}

	void XP::stepPairedVoiceFilter(VoiceState& _owner, VoiceState& _partner, const int32_t _ownerInput,
								   const int32_t _partnerInput)
	{
		const auto structure =
			static_cast<unsigned>((_owner.filterConfig_2000 & FilterConfig::structureMask) >> 12) + 1;
		const auto ownerMode = static_cast<unsigned>((_owner.filterConfig_2000 & FilterConfig::modeMask) >> 10);
		const auto partnerMode =
			static_cast<unsigned>((_owner.filterConfig_2000 & FilterConfig::pairedSecondModeMask) >> 8);
		const auto booster = static_cast<unsigned>((_owner.filterConfig_2000 & FilterConfig::boosterMask) >> 6);
		const auto add16 = [](const int32_t _a, const int32_t _b)
		{ return saturateSigned16(static_cast<int64_t>(_a) + _b); };
		const auto boost16 = [booster](const int32_t _value)
		{ return saturateSigned16(static_cast<int64_t>(_value) << booster); };
		const auto ring = [](const int32_t _modulator, const int32_t _carrier)
		{
			// The modulator alone is clipped to signed Q15.  The carrier keeps the filter's signed 24-bit domain;
			// therefore ring modulation does not collapse high wave-gain signals to a 16-bit result.
			return saturateSigned24((static_cast<int64_t>(saturateSigned16(_modulator)) * _carrier) /
									(int64_t{1} << 15));
		};
		const auto filterOwner = [this, &_owner, ownerMode](const int32_t _input)
		{ return stepVoiceFilter(_owner, _input, ownerMode); };
		const auto filterPartner = [this, &_partner, partnerMode](const int32_t _input)
		{ return stepVoiceFilter(_partner, _input, partnerMode); };

		int32_t output = 0;
		switch (structure)
		{
		case 2:
			output = filterPartner(filterOwner(add16(applyTva(_owner, _ownerInput), _partnerInput)));
			break;
		case 3:
			output = filterPartner(boost16(filterOwner(add16(applyTva(_owner, _ownerInput), _partnerInput))));
			break;
		case 4:
			output = filterPartner(filterOwner(boost16(add16(applyTva(_owner, _ownerInput), _partnerInput))));
			break;
		case 5:
			output = filterPartner(filterOwner(ring(applyTva(_owner, _ownerInput), _partnerInput)));
			break;
		case 6:
			output =
				filterPartner(filterOwner(add16(ring(applyTva(_owner, _ownerInput), _partnerInput), _partnerInput)));
			break;
		case 7:
			output = filterPartner(ring(applyTva(_owner, filterOwner(_ownerInput)), _partnerInput));
			break;
		case 8:
			output =
				filterPartner(add16(ring(applyTva(_owner, filterOwner(_ownerInput)), _partnerInput), _partnerInput));
			break;
		case 9:
			output = ring(applyTva(_owner, filterOwner(_ownerInput)), filterPartner(_partnerInput));
			break;
		case 10:
			{
				const auto filteredPartner = filterPartner(_partnerInput);
				output = add16(ring(applyTva(_owner, filterOwner(_ownerInput)), filteredPartner), filteredPartner);
				break;
			}
		default:
			break;
		}

		_owner.filterOutput_2a00 = static_cast<uint32_t>(applyTva(_partner, output)) & filterMask;
		_partner.filterOutput_2a00 = 0;
	}

	void XP::stepTvaGain(VoiceState& _voice)
	{
		// 0x2700 is updated before the frame's TVA multiplication. Smoothing begins in the starting phase; preload
		// and initialization retain the firmware seed. Reset voices use the truncating decay at the start of step().
		const auto gain = _voice.tvaGain_2700;
		const auto target = (_voice.combinedAmp_2300 & rampScratchMask) >> 4;
		_voice.tvaGain_2700 = static_cast<uint16_t>((uint64_t{7} * gain + target + 3) >> 3);
	}

	void XP::prepareMixer(const size_t _voiceCount)
	{
		// IRAM1/IRAM2 are the mixer's ping-pong sample banks. A destination is cleared only on its first send of this
		// frame; even a zero-level send therefore writes zero, while genuinely unreferenced IRAM slots retain their
		// value.
		m_state.dsp.beginMixerFrame();
		m_mixerSummary = DspMixerSummary{};
		MixerFrame& frame = m_frame;
		for (size_t voiceIndex = 0; voiceIndex < _voiceCount; ++voiceIndex)
		{
			const auto& voice = m_state.voices[voiceIndex];
			const auto output = signExtend(voice.filterOutput_2a00, 24);
			auto& sends = frame[voiceIndex];
			if (output == 0)
			{
				// A silent voice still claims its destinations (the first send of a frame clears the cell) but
				// adds nothing to any sum and cannot move one out of range.
				for (size_t sendIndex = 0; sendIndex < voice.mixer_3a00.size(); ++sendIndex)
				{
					const auto destination = static_cast<size_t>(voice.mixer_3a00[sendIndex] & mixerDestinationMask);
					sends[sendIndex] = {destination, 0};
					if (destination < dsp::nIramSlots)
						m_mixerSummary.seen |= uint64_t{1} << destination;
				}
				continue;
			}
			for (size_t sendIndex = 0; sendIndex < voice.mixer_3a00.size(); ++sendIndex)
			{
				const auto send = voice.mixer_3a00[sendIndex];
				const auto destination = static_cast<size_t>(send & mixerDestinationMask);
				const auto level = static_cast<uint32_t>(send >> mixerLevelShift);
				const auto contribution = (static_cast<int64_t>(output) * level) / (int64_t{1} << mixerProductShift);
				sends[sendIndex] = {destination, contribution};
				m_mixerSummary.add(destination, contribution);

			}
		}
		for (size_t voiceIndex = _voiceCount; voiceIndex < frame.size(); ++voiceIndex)
			frame[voiceIndex] = {};

	}

	bool XP::acceptVoiceEvent(const size_t _voiceIndex, const uint8_t _reason)
	{
		if (m_state.interrupt || m_irqEventAcceptedThisStep)
		{
			m_state.irqBlockedEvent = true;
			return false;
		}

		m_irqEventAcceptedThisStep = true;
		m_state.irqStatus = static_cast<uint16_t>((_voiceIndex << 8) | _reason);
		// Host acknowledgement occurs between complete sample-frame steps in
		// this model. A producer retried here is therefore a later hardware
		// service and asserts the level IRQ again. XP3 only suppresses a producer
		// which physically commits inside the acknowledgement service itself;
		// carrying that suppression into a later frame drops per-voice events.
		m_state.interrupt = true;
		return true;
	}

	void XP::raiseVoiceLoopMarkerEvent(const size_t _voiceIndex, VoiceState& _voice)
	{
		if ((_voice.waveControl_0000 & WaveControl::loopEventInhibit) != 0)
			return;
		if ((_voice.waveControl_0000 & WaveControl::irqEnable) == 0 ||
			(m_state.irqConfigMask & (uint16_t{1} << 5)) == 0)
			return;

		const auto reason = (_voice.waveControl_0000 & WaveControl::loopReason6) != 0
			? IrqReason::alternatePlaybackMarker
			: IrqReason::playbackMarker;
		const auto event = static_cast<uint16_t>((_voiceIndex << 8) | reason);
		// Consecutive producer markers from the voice already owning IRQ coalesce
		// into that event while still advancing the two readable marker stages.
		// A marker belonging to a different voice remains back-pressured.
		if (!(m_state.interrupt && m_state.irqStatus == event) && !acceptVoiceEvent(_voiceIndex, reason))
			return;

		// The producer exposes a two-stage marker. The first qualifying advance
		// sets bit 17; the next sets bit 16, which inhibits further events.
		_voice.waveControl_0000 |= (_voice.waveControl_0000 & WaveControl::loopMarkerFirstStage) != 0
			? WaveControl::loopEventInhibit
			: WaveControl::loopMarkerFirstStage;
	}

	void XP::raiseVoiceRampTerminalEvent(const size_t _voiceIndex, uint32_t& _control, const uint8_t _reason)
	{
		// Bit 16 arms the ramp's terminal reason (TVF-Q/TVF-F/pitch/amp-mod/amp
		// map to 0/1/2/3/4). Delivery sets bit 17 and holds that ramp.
		if ((_control & rampEventArm) == 0 || (m_state.irqConfigMask & (uint16_t{1} << _reason)) == 0)
			return;
		if (!acceptVoiceEvent(_voiceIndex, _reason))
			return;
		_control |= rampHold;
	}

	void XP::raiseVoiceMuteEvent(const size_t _voiceIndex, VoiceState& _voice)
	{
		if ((_voice.waveControl_0000 & (WaveControl::muteRequest | WaveControl::muteStatus)) !=
				WaveControl::muteRequest ||
			(_voice.waveControl_0000 & WaveControl::irqEnable) == 0 ||
			(m_state.irqConfigMask & (uint16_t{1} << IrqReason::muteTransition)) == 0)
			return;
		if (!acceptVoiceEvent(_voiceIndex, IrqReason::muteTransition))
			return;

		_voice.waveControl_0000 |= WaveControl::muteStatus;
	}

	void XP::writeReleaseMask(const size_t _word, const uint16_t _value)
	{
		for (size_t bit = 0; bit < 16; ++bit)
		{
			auto& resetState = m_state.voices[_word * 16 + bit].resetState_3900;
			const bool released = (_value & (uint16_t{1} << bit)) != 0;
			if (resetState.shadow && !released)
			{
				resetState.released = false;
				m_state.voices[_word * 16 + bit].runtimeCache.runtimePhase = VoiceRuntimePhaseCache::parked;
			}
			resetState.shadow = released;
		}
	}

	void XP::commitReleasedVoices()
	{
		for (auto& voice : m_state.voices)
		{
			auto& resetState = voice.resetState_3900;
			if (resetState.shadow && !resetState.released)
			{
				resetState.released = true;
				voice.runtimeCache.runtimePhase = VoiceRuntimePhaseCache::preload;
			}
		}
	}

} // namespace xpLib

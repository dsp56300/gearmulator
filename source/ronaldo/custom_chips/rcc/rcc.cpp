#include "rcc.h"

#include <algorithm>

namespace rccLib
{
	void RCC::reset()
	{
		m_registers = {};
		m_readCapture = 0;
		m_ramA = {};
		m_gains = {};
		m_routing = {};
		m_hostParameters = {};
		m_delayTaps = {};
		m_pendingVoiceSums = {};
		m_previousChorus = {};
		m_previousMixBase = 0;
		m_previousMixSample = 0;
		m_previousRamAddress = 0;
		m_previousDelayHigh = 0;
		m_previousDelayRead = 0;
		m_delayCounter = 0xbbbb;
		m_hasPreviousFrame = false;
		std::fill(m_dram.begin(), m_dram.end(), 0u);
	}

	std::optional<RCC::OutputFrame> RCC::processFrame(const Voices& _voices)
	{
		if (m_registers[0x0d] & 1u) return std::nullopt;
		m_delayCounter = static_cast<uint16_t>(m_delayCounter - 1u);
		if (!m_hasPreviousFrame)
		{
			// Prime the captures used by the first frame.
			m_previousRamAddress = m_routing.voiceWrite[31][0];
			m_previousDelayHigh = m_delayTaps[0] & 0xf000u;
			m_hasPreviousFrame = true;
		}
		const auto output = runProgram(_voices);
		return (m_registers[0x0d] & 0x40u) ? output : OutputFrame{};
	}

	RCC::VoiceProducts RCC::prepareVoices(const Voices& _voices) const
	{
		VoiceProducts products{};
		const uint32_t signMask = (m_registers[0x0d] & 4u) ? 0u : 0x20000u;
		for (size_t voice = 0; voice < VoiceCount; ++voice)
		{
			const uint32_t sample = ((uint32_t(_voices[voice]) & 0x3ffffu) ^ signMask) << 6;
			// Voice 28 has only one multiplier leg in the normal program.
			const size_t legCount = voice == 28 ? 1 : 2;
			for (size_t leg = 0; leg < legCount; ++leg)
			{
				const auto& gain = m_gains.voice[voice][leg];
				products[voice][leg] = alignProduct(multiply24x8(sample, gain.coefficient), gain.shiftFour);
			}
		}
		return products;
	}

	RCC::OutputFrame RCC::runProgram(const Voices& voices)
	{
		ProgramFrame frame{};
		runInputStage(frame, voices);
		runReverbStage(frame);
		return runChorusStage(frame);
	}

	void RCC::runInputStage(ProgramFrame& frame, const Voices& _voices)
	{
		m_ramA[m_previousRamAddress] = m_pendingVoiceSums[0];
		m_ramA[m_routing.voiceWrite[31][1]] = m_pendingVoiceSums[1];

		const uint32_t previousMixBase = m_previousMixBase;
		const uint32_t previousChorusTap2 = m_previousChorus[0];
		const uint32_t previousChorusReturn2 = multiplyAdd(
			previousMixBase,
			previousChorusTap2,
			m_gains.previousChorusReturn[2]);
		const uint32_t mixBaseSample = m_ramA[m_routing.mixBaseSample];
		m_ramA[m_routing.previousChorusReturnWrite2] = previousChorusReturn2;

		frame.voiceProducts = prepareVoices(_voices);
		mixVoices(frame.voiceProducts, 0, 1);

		frame.mixBase = multiplyAdd(mixBaseSample, m_previousMixSample, m_gains.mixBase);
		const uint32_t output1BusSample = m_ramA[m_routing.outputBusSample1];
		m_ramA[m_routing.outputBusSeedWrite1] = multiplyAdd(frame.mixBase, frame.mixBase, m_gains.outputBusSeed1);
		m_ramA[m_routing.outputCaptureWrite1] = multiplySubtract(frame.mixBase, output1BusSample, m_gains.outputCapture1);

		const uint32_t voice01Leg1Input = m_ramA[m_routing.voiceRead[1][1]];
		writeVoicePair(
			{m_ramA[m_routing.voiceRead[1][0]], voice01Leg1Input},
			frame.voiceProducts[1],
			m_routing.voiceWrite[1]);

		const uint32_t chorusSend0Input = m_ramA[m_routing.chorusSendInput0];
		const uint32_t chorusSend0Sample = m_ramA[m_routing.chorusSendSample[0]];
		m_ramA[m_routing.chorusSendSeedWrite[0]] = multiplyAdd(frame.mixBase, voice01Leg1Input, m_gains.chorusSendSeed[0]);
		const uint32_t chorusInputFirstMix = multiplyAdd(
			chorusSend0Sample,
			chorusSend0Input,
			m_gains.chorusInputFirstMix);
		m_ramA[m_routing.chorusInputFirstMixWrite] = chorusInputFirstMix;

		mixVoices(frame.voiceProducts, 2, 3);

		frame.output0Signal = m_ramA[m_routing.outputSignal[0]];
		m_ramA[m_routing.outputSeedWrite[0]] = multiplyAdd(frame.mixBase, frame.mixBase, m_gains.outputSeed[0]);

		frame.output0Bias = m_ramA[m_routing.outputBias[0]];
		const uint32_t chorusToReverb0Sample = m_ramA[m_routing.chorusToReverbSample[0]];
		const uint32_t previousChorusTap3 = m_previousChorus[1];
		const uint32_t previousChorusReturn3 = multiplyAdd(
			previousMixBase,
			previousChorusTap3,
			m_gains.previousChorusReturn[3]);
		const uint32_t previousChorusWithSend0 = multiplyAdd(
			previousChorusReturn3,
			chorusSend0Sample,
			m_gains.previousChorusWithSend[0]);
		const uint32_t chorusSend0Complement = multiplySubtract(
			frame.mixBase,
			previousChorusWithSend0,
			m_gains.chorusSendComplement0);
		const uint32_t chorusToReverb0 = multiplySubtract(
			chorusToReverb0Sample,
			chorusSend0Complement,
			m_gains.chorusToReverb[0]);
		m_ramA[m_routing.chorusToReverbWrite[0]] = chorusToReverb0;

		mixVoices(frame.voiceProducts, 3, 6);

		const uint32_t reverbDry0Sample = m_ramA[m_routing.reverbDrySample[0]];
		const uint32_t reverbSend0Sample = m_ramA[m_routing.reverbSendSample[0]];
		m_ramA[m_routing.reverbSendSeedWrite0] = multiplyAdd(frame.mixBase, reverbDry0Sample, m_gains.reverbSendSeed0);
		const uint16_t previousDelayHigh = m_previousDelayHigh;

		const uint32_t reverbReturn0 = mixReverbReturn(
			frame.mixBase,
			{
				readDelay((previousDelayHigh | (m_delayTaps[0] & 0x0fffu))), readDelay(m_delayTaps[1]),
				readDelay(m_delayTaps[2]), readDelay(m_delayTaps[3]), readDelay(m_delayTaps[4])
			},
			m_gains.reverbReturn[0]);
		const uint32_t reverbReturn0WithDry = multiplyAdd(
			reverbDry0Sample,
			reverbReturn0,
			m_gains.reverbReturnWithDry0);
		const uint32_t auxDelayGain0Sample = m_ramA[m_routing.auxDelayGainSample[0]];

		m_ramA[m_routing.reverbMixWrite[0]] = multiplyAdd(
			reverbReturn0WithDry,
			reverbSend0Sample,
			m_gains.reverbMix[0]);

		mixVoices(frame.voiceProducts, 6, 7);

		const uint32_t reverbSend1Sample = m_ramA[m_routing.reverbSendSample[1]];
		const uint32_t output1Seed = multiplyAdd(frame.mixBase, m_ramA[m_routing.outputSeedSample1], m_gains.outputSeed[1]);
		frame.output1Signal = m_ramA[m_routing.outputSignal[1]];
		m_ramA[m_routing.outputSeedWrite[1]] = output1Seed;

		const uint32_t voice07Leg1Input = m_ramA[m_routing.voiceRead[7][1]];
		m_ramA[m_routing.voiceWrite[7][0]] = addProduct(m_ramA[m_routing.voiceRead[7][0]], frame.voiceProducts[7][0]);
		const uint32_t ramp0IncrementSample = m_ramA[m_routing.rampIncrementSample[0]];
		m_ramA[m_routing.voiceWrite[7][1]] = addProduct(voice07Leg1Input, frame.voiceProducts[7][1]);

		const uint32_t reverbReturn1 = mixReverbReturn(
			frame.mixBase,
			{
				readDelay(m_delayTaps[5]), readDelay(m_delayTaps[6]), readDelay(m_delayTaps[7]),
				readDelay(m_delayTaps[8]), readDelay(m_delayTaps[9])
			},
			m_gains.reverbReturn[1]);

		const uint32_t reverbInputHistory0 = multiplyAdd(
			frame.mixBase,
			readDelay(m_delayTaps[12]),
			m_gains.reverbInputHistory0);
		const uint32_t reverbInputHistoryMix = multiplyAdd(
			reverbInputHistory0,
			readDelay(m_delayTaps[11]),
			m_gains.reverbInputHistoryMix);
		const uint16_t previousDelayRead = m_previousDelayRead;
		const uint32_t auxDelayTransfer0 = multiplyAdd(
			frame.mixBase,
			readDram(previousDelayRead),
			uint8_t(auxDelayGain0Sample >> 16),
			true);

		writeDelay(m_delayTaps[13], auxDelayTransfer0);

		const uint32_t reverbInputFirstMix = multiplyAdd(frame.mixBase, reverbSend0Sample, m_gains.reverbInputFirstMix);
		const uint32_t reverbInput = multiplyAdd(reverbInputFirstMix, reverbSend1Sample, m_gains.reverbInput);
		writeDelay(m_delayTaps[15], reverbInput);

		mixVoices(frame.voiceProducts, 8, 9);

		const uint32_t ramp0State = m_ramA[m_routing.rampState[0]];
		const uint32_t reverbDry1Sample = m_ramA[m_routing.reverbDrySample[1]];
		const uint32_t reverbReturn1Gain = multiplyAdd(frame.mixBase, reverbReturn1, m_gains.reverbReturnGain1);
		const uint32_t reverbReturn1WithSend = multiplyAdd(
			reverbReturn1Gain,
			reverbSend1Sample,
			m_gains.reverbReturnWithSend1);
		const uint32_t ramp0LimitSample = m_ramA[m_routing.rampLimitSample[0]];
		const uint32_t voice09Leg0Input = m_ramA[m_routing.voiceRead[9][0]];
		m_ramA[m_routing.reverbMixWrite[1]] = multiplySubtract(
			reverbReturn1WithSend,
			reverbDry1Sample,
			m_gains.reverbMix[1]);

		frame.reverbPredelaySample = readDelay(m_delayTaps[16]);
		writeDelay(m_delayTaps[17], multiplyAdd(reverbInputHistoryMix, reverbInput, m_gains.reverbFilteredInput));

		const uint32_t ramp0ResetSample = m_ramA[m_routing.rampResetSample[0]];
		m_ramA[m_routing.voiceWrite[9][0]] = addProduct(voice09Leg0Input, frame.voiceProducts[9][0]);

		const uint32_t ramp0Increment = multiplyAdd(ramp0IncrementSample, ramp0IncrementSample, m_gains.rampIncrement0);
		const uint32_t ramp0Candidate = multiplySubtract(ramp0Increment, ramp0State, m_gains.rampCandidate[0]);
		const uint32_t ramp0Wrapped = wrapRamp(
			ramp0Candidate,
			ramp0LimitSample,
			ramp0ResetSample,
			m_gains.rampLimit[0]);
		m_ramA[m_routing.rampNextWrite[0]] = multiplyAdd(ramp0Wrapped, voice09Leg0Input, m_gains.rampNext[0]);

		m_ramA[m_routing.voiceWrite[9][1]] = addProduct(m_ramA[m_routing.voiceRead[9][1]], frame.voiceProducts[9][1]);

	}

	void RCC::runReverbStage(ProgramFrame& frame)
	{
		mixVoices(frame.voiceProducts, 10, 11);

		frame.output2Signal = m_ramA[m_routing.outputSignal[2]];
		m_ramA[m_routing.outputSeedWrite[2]] = multiplyAdd(frame.mixBase, frame.mixBase, m_gains.outputSeed[2]);

		const auto reverbDiffuser0 = processDelaySection(
			frame.reverbPredelaySample,
			readDelay(m_delayTaps[18]),
			m_gains.diffuserInput[0],
			m_gains.diffuserFeedback[0]);
		const auto reverbDiffuser1 = processDelaySection(
			reverbDiffuser0.output,
			readDelay(m_delayTaps[19]),
			m_gains.diffuserInput[1],
			m_gains.diffuserFeedback[1]);
		writeDelay(m_delayTaps[20], reverbDiffuser0.internal);
		writeDelay(m_delayTaps[21], reverbDiffuser1.internal);

		frame.output2Bias = m_ramA[m_routing.outputBias[2]];
		mixVoices(frame.voiceProducts, 11, 13);

		const auto reverbDiffuser2 = processDelaySection(
			reverbDiffuser1.output,
			readDelay(m_delayTaps[22]),
			m_gains.diffuserInput[2],
			m_gains.diffuserFeedback[2]);
		writeDelay(m_delayTaps[24], reverbDiffuser2.internal);

		const uint32_t chorusSend1Sample = m_ramA[m_routing.chorusSendSample[1]];
		m_ramA[m_routing.chorusSendSeedWrite[1]] = multiplyAdd(frame.mixBase, frame.mixBase, m_gains.chorusSendSeed[1]);
		const uint32_t chorusSend1Capture = multiplySubtract(frame.mixBase, chorusSend1Sample, m_gains.chorusSendCapture1);
		m_ramA[m_routing.chorusSendCaptureWrite1] = chorusSend1Capture;
		const uint32_t previousChorusReturn2Sample = m_ramA[m_routing.previousChorusReturnSample2];
		const uint32_t chorusToReverb1Sample = m_ramA[m_routing.chorusToReverbSample[1]];
		const uint32_t previousChorusWithSend1 = multiplyAdd(
			previousChorusReturn2Sample,
			chorusSend1Capture,
			m_gains.previousChorusWithSend[1]);
		const uint32_t chorusToReverb1 = multiplyAdd(
			previousChorusWithSend1,
			chorusToReverb1Sample,
			m_gains.chorusToReverb[1]);
		const uint32_t voice13Leg0Input = m_ramA[m_routing.voiceRead[13][0]];
		m_ramA[m_routing.chorusToReverbWrite[1]] = chorusToReverb1;

		writeVoicePair(
			{voice13Leg0Input, m_ramA[m_routing.voiceRead[13][1]]},
			frame.voiceProducts[13],
			m_routing.voiceWrite[13]);
		const uint32_t voice14Leg1Input = m_ramA[m_routing.voiceRead[14][1]];
		writeVoicePair(
			{m_ramA[m_routing.voiceRead[14][0]], voice14Leg1Input},
			frame.voiceProducts[14],
			m_routing.voiceWrite[14]);

		frame.output3Signal = m_ramA[m_routing.outputSignal[3]];
		m_ramA[m_routing.outputSeedWrite[3]] = multiplyAdd(frame.mixBase, voice14Leg1Input, m_gains.outputSeed[3]);

		frame.output3Bias = m_ramA[m_routing.outputBias[3]];
		mixVoices(frame.voiceProducts, 15, 16);

		const uint32_t reverbFeedbackBase = multiplyAdd(frame.mixBase, frame.mixBase, m_gains.reverbFeedbackBase);
		const uint32_t reverbDampingHistory = multiplyAdd(
			reverbFeedbackBase,
			readDelay(m_delayTaps[25]),
			m_gains.reverbDampingHistory);
		const uint32_t reverbFeedbackGain = multiplyAdd(
			frame.mixBase,
			readDelay(m_delayTaps[26]),
			m_gains.reverbFeedbackGain);
		const uint32_t reverbDampedFeedback = multiplyAdd(
			reverbDampingHistory,
			reverbFeedbackGain,
			m_gains.reverbDampedFeedback);
		const uint32_t reverbTailInput = multiplyAdd(
			reverbDampedFeedback,
			reverbDiffuser2.output,
			m_gains.reverbTailInput);
		const auto reverbTail0 = processDelaySection(
			reverbTailInput,
			readDelay(m_delayTaps[27]),
			m_gains.tailInput[0],
			m_gains.tailFeedback[0]);
		writeDelay(m_delayTaps[28], reverbFeedbackGain);
		writeDelay(m_delayTaps[29], reverbDampedFeedback);
		writeDelay(m_delayTaps[30], reverbTail0.internal);
		writeDelay(m_delayTaps[31], reverbTail0.output);

		const uint32_t ramp1State = m_ramA[m_routing.rampState[1]];
		const uint32_t ramp1IncrementSample = m_ramA[m_routing.rampIncrementSample[1]];
		const uint32_t ramp1LimitSample = m_ramA[m_routing.rampLimitSample[1]];
		const uint32_t ramp1ResetSample = m_ramA[m_routing.rampResetSample[1]];
		m_ramA[m_routing.voiceWrite[16][0]] = addProduct(m_ramA[m_routing.voiceRead[16][0]], frame.voiceProducts[16][0]);

		const uint32_t ramp1Candidate = multiplySubtract(ramp1IncrementSample, ramp1State, m_gains.rampCandidate[1]);
		const uint32_t ramp1Wrapped = wrapRamp(
			ramp1Candidate,
			ramp1LimitSample,
			ramp1ResetSample,
			m_gains.rampLimit[1]);
		m_ramA[m_routing.rampNextWrite[1]] = multiplyAdd(ramp1Wrapped, ramp1Candidate, m_gains.rampNext[1]);

		const uint32_t ramp1RangeSample = m_ramA[m_routing.rampRangeSample1];
		m_ramA[m_routing.voiceWrite[16][1]] = addProduct(m_ramA[m_routing.voiceRead[16][1]], frame.voiceProducts[16][1]);

		const uint32_t voice17Leg0Input = m_ramA[m_routing.voiceRead[17][0]];
		m_ramA[m_routing.rampRangeCaptureWrite1] = multiplyAdd(frame.mixBase, ramp1RangeSample, m_gains.rampRangeCapture1);

		m_ramA[m_routing.voiceWrite[17][0]] = addProduct(voice17Leg0Input, frame.voiceProducts[17][0]);
		const uint32_t chorusInputSecondSample = m_ramA[m_routing.chorusInputSecondSample];
		m_ramA[m_routing.voiceWrite[17][1]] = addProduct(m_ramA[m_routing.voiceRead[17][1]], frame.voiceProducts[17][1]);

		const uint32_t chorusInputFirstSample = m_ramA[m_routing.chorusInputFirstSample];
		const uint32_t chorusInputMix = multiplyAdd(
			chorusInputFirstSample,
			chorusInputSecondSample,
			m_gains.chorusInputMix);
		const uint32_t voice18Leg0Input = m_ramA[m_routing.voiceRead[18][0]];
		m_ramA[m_routing.chorusInputMixWrite] = chorusInputMix;

		m_ramA[m_routing.voiceWrite[18][0]] = addProduct(voice18Leg0Input, frame.voiceProducts[18][0]);
		m_ramA[m_routing.voiceWrite[18][1]] = addProduct(m_ramA[m_routing.voiceRead[18][1]], frame.voiceProducts[18][1]);

		frame.output4Signal = m_ramA[m_routing.outputSignal[4]];
		m_ramA[m_routing.outputSeedWrite[4]] = multiplyAdd(frame.mixBase, frame.mixBase, m_gains.outputSeed[4]);

		frame.output4Bias = m_ramA[m_routing.outputBias[4]];
		mixVoices(frame.voiceProducts, 19, 20);

		const auto reverbTail1 = processDelaySection(
			readDelay(m_delayTaps[33]),
			readDelay(m_delayTaps[32]),
			m_gains.tailInput[1],
			m_gains.tailFeedback[1]);
		const auto reverbTail2 = processDelaySection(
			readDelay(m_delayTaps[35]),
			readDelay(m_delayTaps[34]),
			m_gains.tailInput[2],
			m_gains.tailFeedback[2]);
		writeDelay(m_delayTaps[36], reverbTail1.output);
		writeDelay(m_delayTaps[37], reverbTail2.internal);
		writeDelay(m_delayTaps[38], reverbTail2.output);
		writeDelay(m_delayTaps[39], multiplySubtract(frame.mixBase, reverbTail1.internal, m_gains.reverbTailStorage1));

		frame.chorusFold0Source = m_ramA[m_routing.chorusFoldSource[0]];

	}

	RCC::OutputFrame RCC::runChorusStage(ProgramFrame& frame)
	{
		const uint32_t chorusPhase0Base = m_ramA[m_routing.chorusPhaseBase[0]];
		const uint32_t voice20Leg0Input = m_ramA[m_routing.voiceRead[20][0]];
		const uint32_t chorusPhase0 = foldPhase(
			chorusPhase0Base,
			frame.chorusFold0Source,
			m_gains.phaseFold[0],
			PhaseFold::Positive);
		m_ramA[m_routing.chorusPhaseWrite[0]] = chorusPhase0;

		m_ramA[m_routing.voiceWrite[20][0]] = addProduct(voice20Leg0Input, frame.voiceProducts[20][0]);
		const uint32_t auxDelayGain1Sample = m_ramA[m_routing.auxDelayGainSample[1]];
		m_ramA[m_routing.voiceWrite[20][1]] = addProduct(m_ramA[m_routing.voiceRead[20][1]], frame.voiceProducts[20][1]);
		mixVoices(frame.voiceProducts, 21, 22);
		const uint32_t auxDelaySource2 = m_ramA[m_routing.auxDelaySource2];
		const uint32_t chorusPhase1BaseSample = m_ramA[m_routing.chorusPhaseBaseSample1];
		mixVoices(frame.voiceProducts, 22, 23);

		const uint32_t chorusFold1Source = m_ramA[m_routing.chorusFoldSource[1]];
		const uint32_t chorusPhase1OffsetSample = m_ramA[m_routing.chorusPhaseOffsetSample[1]];
		const uint32_t chorusPhase1Base = multiplyAdd(frame.mixBase, chorusPhase1BaseSample, m_gains.chorusPhaseBase1);
		const uint32_t chorusPhase1Fold = foldPhase(
			chorusPhase1Base,
			chorusFold1Source,
			m_gains.phaseFold[1],
			PhaseFold::Negative);
		m_ramA[m_routing.chorusPhaseFoldWrite1] = chorusPhase1Fold;

		const uint32_t output5Signal = m_ramA[m_routing.outputSignal[5]];
		const uint32_t output5Bias = m_ramA[m_routing.outputBias[5]];
		m_ramA[m_routing.outputSeedWrite[5]] = multiplyAdd(frame.mixBase, frame.mixBase, m_gains.outputSeed[5]);

		const uint32_t chorusTap0 = readChorusTap(chorusPhase0);
		const uint32_t auxDelayTransfer1 = multiplyAdd(
			frame.mixBase,
			readDelay(m_delayTaps[40]),
			uint8_t(auxDelayGain1Sample >> 16),
			true);

		writeDelay(m_delayTaps[43], auxDelayTransfer1);
		writeDelay(m_delayTaps[44], multiplyAdd(auxDelaySource2, frame.mixBase, m_gains.auxDelayTransfer2));

		mixVoices(frame.voiceProducts, 23, 25);

		const uint32_t chorusPhase1 = multiplySubtract(
			chorusPhase1Fold,
			chorusPhase1OffsetSample,
			m_gains.chorusPhase[1]);
		const uint32_t chorusInputSample = m_ramA[m_routing.chorusInputSample];
		m_ramA[m_routing.chorusPhaseResidualWrite1] = multiplyAdd(
			chorusPhase1,
			chorusPhase1,
			m_gains.chorusPhaseResidual1);

		const uint32_t chorusReturn0SendSample = m_ramA[m_routing.chorusReturnSendSample[0]];
		const uint32_t chorusReturn0Gain = multiplyAdd(frame.mixBase, chorusTap0, m_gains.chorusReturnGain[0]);
		const uint32_t chorusReturn0WithInput = multiplyAdd(
			chorusReturn0Gain,
			chorusInputSample,
			m_gains.chorusReturnWithInput0);
		const uint32_t chorusReturnMix0 = multiplySubtract(
			chorusReturn0WithInput,
			chorusReturn0SendSample,
			m_gains.chorusReturnMix[0]);
		m_ramA[m_routing.chorusReturnMixWrite[0]] = chorusReturnMix0;

		mixVoices(frame.voiceProducts, 25, 26);

		const uint32_t chorusInputGain = multiplyAdd(frame.mixBase, chorusInputSample, m_gains.chorusInputGain);
		const uint32_t chorusReturn1SendSample = m_ramA[m_routing.chorusReturnSendSample[1]];
		const uint32_t chorusCrossfeedSample = m_ramA[m_routing.chorusCrossfeedSample];
		m_ramA[m_routing.chorusInputResidualWrite] = multiplyAdd(chorusInputGain, frame.mixBase, m_gains.chorusInputResidual);

		const uint32_t chorusReturn1Gain = multiplyAdd(
			frame.mixBase,
			readChorusTap(chorusPhase1),
			m_gains.chorusReturnGain[1]);
		const uint32_t chorusReturn1WithSend = multiplyAdd(
			chorusReturn1Gain,
			chorusReturn1SendSample,
			m_gains.chorusReturnWithSend1);
		const uint32_t chorusReturnMix1 = multiplyAdd(
			chorusReturn1WithSend,
			chorusCrossfeedSample,
			m_gains.chorusReturnMix[1]);

		m_ramA[m_routing.chorusReturnMixWrite[1]] = chorusReturnMix1;

		mixVoices(frame.voiceProducts, 26, 27);

		const uint32_t output6Signal = m_ramA[m_routing.outputSignal[6]];
		const uint32_t output6Bias = m_ramA[m_routing.outputBias[6]];
		m_ramA[m_routing.outputSeedWrite[6]] = multiplyAdd(frame.mixBase, frame.mixBase, m_gains.outputSeed[6]);

		const uint32_t voice27Leg1Input = m_ramA[m_routing.voiceRead[27][1]];
		writeVoicePair(
			{m_ramA[m_routing.voiceRead[27][0]], voice27Leg1Input},
			frame.voiceProducts[27],
			m_routing.voiceWrite[27]);

		const uint32_t chorusFold2Source = m_ramA[m_routing.chorusFoldSource[2]];
		const uint32_t chorusPhase2Base = m_ramA[m_routing.chorusPhaseBase[2]];
		const uint32_t mixBaseUpdateSample = m_ramA[m_routing.mixBaseUpdateSample];
		m_ramA[m_routing.mixBaseUpdateWrite] = multiplyAdd(
			mixBaseUpdateSample,
			chorusPhase2Base,
			m_gains.mixBaseUpdate);

		const uint32_t voice28Base = multiplyAdd(chorusFold2Source, voice27Leg1Input, m_gains.voice28Base);
		const uint32_t voice28Leg0Sum = addProduct(voice28Base, frame.voiceProducts[28][0]);
		const uint32_t chorusFold3Source = m_ramA[m_routing.chorusFoldSource[3]];
		const uint32_t chorusPhase3Base = m_ramA[m_routing.chorusPhaseBase[3]];
		m_ramA[m_routing.voice28RoutedSumWrite] = multiplyAdd(voice28Leg0Sum, frame.mixBase, m_gains.voice28RoutedSum);

		writeDelay(m_delayTaps[53], multiplyAdd(chorusInputGain, chorusCrossfeedSample, m_gains.chorusDelayInput));

		const uint32_t chorusPhase2FeedbackSample = m_ramA[m_routing.chorusPhaseFeedbackSample2];
		const uint32_t chorusPhase2 = foldPhase(
			chorusPhase2Base,
			chorusFold2Source,
			m_gains.phaseFold[2],
			PhaseFold::Positive);
		const uint32_t chorusPhase2Feedback = multiplySubtract(
			chorusPhase2FeedbackSample,
			chorusPhase2,
			m_gains.chorusPhaseFeedback2);
		const uint32_t chorusPhase3OffsetSample = m_ramA[m_routing.chorusPhaseOffsetSample[3]];
		m_ramA[m_routing.chorusPhaseFeedbackWrite2] = chorusPhase2Feedback;
		const uint32_t chorusPhase3Fold = foldPhase(
			chorusPhase3Base,
			chorusFold3Source,
			m_gains.phaseFold[3],
			PhaseFold::Negative);
		const uint32_t chorusPhase3 = multiplySubtract(
			chorusPhase3OffsetSample,
			chorusPhase3Fold,
			m_gains.chorusPhase[3]);
		m_ramA[m_routing.chorusPhaseWrite[3]] = chorusPhase3;

		mixVoices(frame.voiceProducts, 29, 30);
		const uint32_t voice30Leg1Input = m_ramA[m_routing.voiceRead[30][1]];
		writeVoicePair(
			{m_ramA[m_routing.voiceRead[30][0]], voice30Leg1Input},
			frame.voiceProducts[30],
			m_routing.voiceWrite[30]);

		const uint32_t output7Signal = m_ramA[m_routing.outputSignal[7]];
		m_ramA[m_routing.outputSeedWrite[7]] = multiplyAdd(frame.mixBase, voice30Leg1Input, m_gains.outputSeed[7]);

		m_pendingVoiceSums[0] = addProduct(m_ramA[m_routing.voiceRead[31][0]], frame.voiceProducts[31][0]);
		const uint32_t voice31Leg1Input = m_ramA[m_routing.voiceRead[31][1]];
		m_pendingVoiceSums[1] = addProduct(voice31Leg1Input, frame.voiceProducts[31][1]);

		const auto chorusTaps23 = readChorusPair({chorusPhase2, chorusPhase3});

		m_previousChorus[1] = chorusTaps23[1];
		m_previousMixBase = frame.mixBase;
		m_previousChorus[0] = chorusTaps23[0];
		m_previousRamAddress = m_routing.voiceWrite[31][0];
		m_previousDelayHigh = uint16_t(m_delayTaps[0] & 0xf000u);
		m_previousMixSample = voice31Leg1Input;
		m_previousDelayRead = uint16_t(m_delayTaps[63] + m_delayCounter);

		const uint32_t output7Bias = m_ramA[m_routing.outputBias[7]];
		return mixOutputs(
			{frame.output0Bias, frame.mixBase, frame.output2Bias, frame.output3Bias, frame.output4Bias, output5Bias, output6Bias, output7Bias},
			{
				frame.output0Signal, frame.output1Signal, frame.output2Signal, frame.output3Signal, frame.output4Signal, output5Signal,
				output6Signal, output7Signal
			});
	}

	void RCC::mixVoices(const VoiceProducts& _products, const size_t _first, const size_t _end)
	{
		for (size_t voice = _first; voice < _end; ++voice)
		{
			const auto& reads = m_routing.voiceRead[voice];
			writeVoicePair({m_ramA[reads[0]], m_ramA[reads[1]]}, _products[voice], m_routing.voiceWrite[voice]);
		}
	}

	void RCC::writeVoicePair(const std::array<uint32_t, 2>& _inputs,
		const std::array<uint32_t, 2>& _products, const std::array<uint8_t, 2>& _destinations)
	{
		// Inputs are captured by the caller before either destination is overwritten.
		for (size_t i = 0; i < _inputs.size(); ++i)
			m_ramA[_destinations[i]] = addProduct(_inputs[i], _products[i]);
	}

	uint32_t RCC::mixReverbReturn(const uint32_t _base, const std::array<uint32_t, 5>& _samples,
		const std::array<Gain, 6>& _gains) const
	{
		uint32_t sum = _base;
		for (size_t i = 0; i < 3; ++i)
			sum = multiplyAdd(sum, _samples[i], _gains[i]);
		sum = multiplyAdd(_base, sum, _gains[3]);
		for (size_t i = 3; i < _samples.size(); ++i)
			sum = multiplyAdd(sum, _samples[i], _gains[i + 1]);
		return sum;
	}

	RCC::OutputFrame RCC::mixOutputs(const std::array<uint32_t, 8>& _biases,
		const std::array<uint32_t, 8>& _signals) const
	{
		std::array<uint32_t, 8> sums{};
		for (size_t i = 0; i < sums.size(); ++i)
			sums[i] = multiplyAdd(_biases[i], _signals[i], m_gains.output[i]);
		return makeOutput(sums, m_registers[0x0d]);
	}

	uint32_t RCC::readChorusTap(const uint32_t _phase) const
	{
		const uint16_t offset = uint16_t((_phase >> 12) | ((_phase & 0x800000u) ? 0xf000u : 0u));
		return interpolate(_phase, readDelay(offset), readDelay(offset + 1u));
	}

	std::array<uint32_t, 2> RCC::readChorusPair(const std::array<uint32_t, 2>& _phases) const
	{
		return {readChorusTap(_phases[0]), readChorusTap(_phases[1])};
	}

	uint32_t RCC::readDram(const uint32_t _address) const
	{
		return m_dram[_address & m_dramWordMask];
	}

	uint32_t RCC::readDelay(const uint32_t _offset) const
	{
		return readDram(_offset + m_delayCounter);
	}

	void RCC::writeDelay(const uint32_t _offset, const uint32_t _sample)
	{
		m_dram[(_offset + m_delayCounter) & m_dramWordMask] = _sample;
	}

	uint32_t RCC::saturateResult(const uint32_t _result29)
	{
		const uint32_t word = _result29 & Word29Mask;
		const int64_t signedWord = int64_t(word) - ((word & 0x10000000u) ? 0x20000000ll : 0ll);
		if (signedWord > 0x7fffff)
			return 0x7fffffu;
		if (signedWord < -0x800000)
			return 0x800000u;
		return word & Word24Mask;
	}

	uint32_t RCC::multiply24x8(const uint32_t _operand, const uint8_t _coefficient)
	{
		const uint32_t word = _operand & Word24Mask;
		const int64_t operand = int64_t(word) - ((word & 0x800000u) ? 0x1000000ll : 0ll);
		const int64_t coefficient = int64_t(_coefficient) - ((_coefficient & 0x80u) ? 256ll : 0ll);
		return static_cast<uint32_t>(operand * coefficient);
	}

	uint32_t RCC::shiftProduct(const uint32_t _product)
	{
		return (_product >> 4) | ((_product & 0x80000000u) ? 0xf0000000u : 0u);
	}

	uint32_t RCC::accumulate(const uint32_t _a24, const uint32_t _formatted32, const unsigned _carry)
	{
		const uint32_t a = (_a24 & Word24Mask) | ((_a24 & 0x800000u) ? 0x1f000000u : 0u);
		return (a + (_formatted32 >> 3) + _carry) & Word29Mask;
	}

	uint32_t RCC::accumulateSaturated(const uint32_t _a24, const uint32_t _formatted32, const unsigned _carry)
	{
		return saturateResult(accumulate(_a24, _formatted32, _carry));
	}

	uint32_t RCC::alignProduct(const uint32_t _product, const bool _shiftFour)
	{
		return _shiftFour ? shiftProduct(_product) : _product;
	}

	uint32_t RCC::addProduct(const uint32_t _addend, const uint32_t _formatted)
	{
		return accumulateSaturated(_addend, _formatted,
			(_formatted & 7u) >= (4u + (_formatted >> 31)));
	}

	uint32_t RCC::subtractProduct(const uint32_t _addend, const uint32_t _formatted)
	{
		const uint32_t complemented = ~_formatted;
		return accumulateSaturated(_addend, complemented,
			(complemented & 7u) >= (3u + (complemented >> 31)));
	}

	uint32_t RCC::multiplyAdd(const uint32_t _addend, const uint32_t _input, const uint8_t _coefficient, const bool _shiftFour)
	{
		return addProduct(_addend, alignProduct(multiply24x8(_input, _coefficient), _shiftFour));
	}

	uint32_t RCC::multiplySubtract(const uint32_t _addend, const uint32_t _input, const uint8_t _coefficient, const bool _shiftFour)
	{
		return subtractProduct(_addend, alignProduct(multiply24x8(_input, _coefficient), _shiftFour));
	}

	uint32_t RCC::multiplyAdd(const uint32_t _addend, const uint32_t _input, const Gain& _gain)
	{
		return multiplyAdd(_addend, _input, _gain.coefficient, _gain.shiftFour);
	}

	uint32_t RCC::multiplySubtract(const uint32_t _addend, const uint32_t _input, const Gain& _gain)
	{
		return multiplySubtract(_addend, _input, _gain.coefficient, _gain.shiftFour);
	}

	uint32_t RCC::foldPhase(const uint32_t _base, const uint32_t _source,
		const Gain& _gain, const PhaseFold _direction)
	{
		const uint32_t product = multiply24x8(_source, _gain.coefficient);
		const bool complement = ((product & 0x80000000u) != 0) == (_direction == PhaseFold::Positive);
		const uint32_t aligned = alignProduct(product, _gain.shiftFour);
		const uint32_t formatted = complement ? ~aligned : aligned;
		return accumulateSaturated(_base, formatted,
			(formatted & 7u) >= (4u + (formatted >> 31) - unsigned(complement)));
	}

	uint32_t RCC::wrapRamp(const uint32_t _candidate, const uint32_t _limit, const uint32_t _reset,
		const Gain& _gain)
	{
		const uint32_t complemented = ~alignProduct(
			multiply24x8(_limit, _gain.coefficient), _gain.shiftFour);
		// This comparison uses the raw 29-bit sign, before saturation.
		const uint32_t test = accumulate(_candidate, complemented,
			(complemented & 7u) >= (3u + (complemented >> 31)));
		return (test & 0x10000000u) ? _candidate : _reset;
	}

	RCC::DelaySection RCC::processDelaySection(const uint32_t _input, const uint32_t _delayed,
		const Gain& _inputGain, const Gain& _feedbackGain)
	{
		const uint32_t internal = multiplyAdd(_input, _delayed, _inputGain);
		return {internal, multiplyAdd(_delayed, internal, _feedbackGain)};
	}

	uint32_t RCC::interpolate(const uint32_t _phase, const uint32_t _first, const uint32_t _adjacent)
	{
		const uint8_t fraction = uint8_t(((_phase >> 5) & 0x7fu) | ((_phase >> 16) & 0x80u));
		// Keep the original two rounding and saturation cuts, including negative fractions.
		const uint32_t first = multiplySubtract(_first, _first, fraction, true);
		return multiplyAdd(first, _adjacent, fraction, true);
	}

	int32_t RCC::outputWord(const uint32_t _feedback, const uint8_t _configuration)
	{
		// D6 clears only the serializer MSB; processFrame separately mutes board outputs.
		const uint32_t word = (_feedback >> 8) & ((_configuration & 0x40u) ? 0xffffu : 0x7fffu);
		return int32_t(word) - ((word & 0x8000u) ? 0x10000 : 0);
	}

	RCC::OutputFrame RCC::makeOutput(const std::array<uint32_t, 8>& _sums, const uint8_t _configuration)
	{
		OutputFrame result{};
		for (size_t i = 0; i < _sums.size(); ++i)
			result.serialLoadWords[i] = outputWord(_sums[i], _configuration);
		return result;
	}

}

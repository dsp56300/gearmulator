#include "rcc.h"

namespace rccLib
{
	uint8_t RCC::read(const uint16_t _offset) const
	{
		return static_cast<uint8_t>(readBus(_offset));
	}

	void RCC::write(const uint16_t _offset, const uint8_t _data)
	{
		const uint8_t offset = _offset & 0x0f;
		m_registers[offset] = _data;
		if (offset != 4 && offset != 6 && offset != 8 && offset != 0x0a)
			return;
		if (offset == 8) loadReadback(m_ramA[_data & 31u]);
		else if (offset == 0x0a) loadReadback(m_hostParameters[_data]);
		else commitHostWrite(offset == 4, _data);
	}

	uint16_t RCC::readBus(const uint16_t _offset) const
	{
		const uint8_t offset = _offset & 0x0f;
		uint8_t low = 0xff;
		switch (offset)
		{
		case 0: low = static_cast<uint8_t>(m_readCapture >> 16); break;
		case 1: low = static_cast<uint8_t>(m_readCapture >> 8); break;
		case 2: low = static_cast<uint8_t>(m_readCapture); break;
		}
		const uint8_t high = (m_registers[0x0d] & 2u) ? 0xff : static_cast<uint8_t>(m_readCapture >> 8);
		return (uint16_t(high) << 8) | low;
	}

	void RCC::writeBus(const uint16_t _offset, const uint16_t _data, const bool _g217)
	{
		// Every capture uses the mode before this closure. In particular a paired
		// C/D write may itself clear D0, but both banks were enabled by the old D0.
		const bool modeM = (m_registers[0x0d] & 1u) || _g217;
		const uint8_t low = static_cast<uint8_t>(_data);
		const uint8_t selected = modeM ? static_cast<uint8_t>(_data >> 8) : low;
		switch (_offset & 0x0fu)
		{
		case 0:
			write(0, selected);
			if (modeM) write(1, low);
			break;
		case 2: write(2, selected); break;
		case 0x0c:
			write(0x0c, selected);
			if (modeM) write(0x0d, low);
			break;
		case 1: case 4: case 6: case 8: case 0x0a: case 0x0d:
			write(_offset, low);
			break;
		}
	}

	void RCC::loadReadback(const uint32_t _word)
	{
		// Readback has its own capture bank; it must not overwrite write staging.
		m_readCapture = _word & 0xffffffu;
	}

	uint32_t RCC::hostPayload() const
	{
		return (uint32_t(m_registers[0]) << 16) | (uint32_t(m_registers[1]) << 8) | m_registers[2];
	}

	void RCC::commitHostWrite(const bool _ramA, const uint8_t _address)
	{
		const uint16_t index = _ramA ? _address & 31u : _address;
		const uint32_t word = hostPayload() & (_ramA ? 0xffffffu : 0x3ffffu);
		if (_ramA) m_ramA[index] = word;
		else
		{
			m_hostParameters[index] = word;
			decodeParameter(static_cast<uint8_t>(index), word);
			const unsigned shift = (3u - (index & 3u)) * 4u;
			auto& tap = m_delayTaps[index >> 2];
			tap = static_cast<uint16_t>((tap & ~(15u << shift)) | (((word >> 14) & 15u) << shift));
		}
	}

	void RCC::decodeParameter(const uint8_t _address, const uint32_t _word)
	{
		const Gain gain{uint8_t(_word), bool(_word & 0x100u)};
		const uint8_t address = uint8_t((_word >> 9) & 31u);
		switch (_address)
		{
		case 0: m_routing.voiceWrite[31][0] = address; break;
		case 1: m_gains.previousChorusReturn[2] = gain; m_routing.voiceWrite[31][1] = address; break;
		case 2: m_gains.mixBase = gain; m_routing.mixBaseSample = address; break;
		case 3: m_gains.previousChorusReturn[3] = gain; m_routing.previousChorusReturnWrite2 = address; break;
		case 4: m_gains.voice[0][0] = gain; m_routing.voiceRead[0][0] = address; break;
		case 5: m_gains.voice[0][1] = gain; m_routing.voiceRead[0][1] = address; break;
		case 6: m_routing.voiceWrite[0][0] = address; break;
		case 7: m_gains.reverbReturn[0][0] = gain; m_routing.voiceWrite[0][1] = address; break;
		case 8: m_gains.outputBusSeed1 = gain; m_routing.outputBusSample1 = address; break;
		case 9: m_gains.outputCapture1 = gain; break;
		case 10: m_routing.outputBusSeedWrite1 = address; break;
		case 11: m_gains.reverbReturn[0][1] = gain; m_routing.outputCaptureWrite1 = address; break;
		case 12: m_gains.voice[1][0] = gain; m_routing.voiceRead[1][0] = address; break;
		case 13: m_gains.voice[1][1] = gain; m_routing.voiceRead[1][1] = address; break;
		case 14: m_routing.voiceWrite[1][0] = address; break;
		case 15: m_gains.reverbReturn[0][2] = gain; m_routing.voiceWrite[1][1] = address; break;
		case 16: m_gains.chorusSendSeed[0] = gain; m_routing.chorusSendInput0 = address; break;
		case 17: m_gains.chorusInputFirstMix = gain; m_routing.chorusSendSample[0] = address; break;
		case 18: m_gains.reverbReturn[0][3] = gain; m_routing.chorusSendSeedWrite[0] = address; break;
		case 19: m_gains.previousChorusWithSend[0] = gain; m_routing.chorusInputFirstMixWrite = address; break;
		case 20: m_gains.reverbReturn[0][4] = gain; break;
		case 21: m_gains.voice[2][0] = gain; m_routing.voiceRead[2][0] = address; break;
		case 22: m_gains.voice[2][1] = gain; m_routing.voiceRead[2][1] = address; break;
		case 23: m_routing.voiceWrite[2][0] = address; break;
		case 24: m_gains.chorusSendComplement0 = gain; m_routing.voiceWrite[2][1] = address; break;
		case 25: m_gains.outputSeed[0] = gain; m_routing.outputSignal[0] = address; break;
		case 26: m_gains.reverbReturn[0][5] = gain; break;
		case 27: m_gains.reverbReturn[1][0] = gain; m_routing.outputSeedWrite[0] = address; break;
		case 28: m_gains.output[0] = gain; m_routing.outputBias[0] = address; break;
		case 29: m_gains.chorusToReverb[0] = gain; m_routing.chorusToReverbSample[0] = address; break;
		case 31: m_gains.reverbReturn[1][1] = gain; m_routing.chorusToReverbWrite[0] = address; break;
		case 32: m_gains.voice[3][0] = gain; m_routing.voiceRead[3][0] = address; break;
		case 33: m_gains.voice[3][1] = gain; m_routing.voiceRead[3][1] = address; break;
		case 34: m_routing.voiceWrite[3][0] = address; break;
		case 35: m_gains.reverbReturn[1][2] = gain; m_routing.voiceWrite[3][1] = address; break;
		case 36: m_gains.voice[4][0] = gain; m_routing.voiceRead[4][0] = address; break;
		case 37: m_gains.voice[4][1] = gain; m_routing.voiceRead[4][1] = address; break;
		case 38: m_gains.reverbReturn[1][3] = gain; m_routing.voiceWrite[4][0] = address; break;
		case 39: m_routing.voiceWrite[4][1] = address; break;
		case 40: m_gains.reverbReturn[1][4] = gain; break;
		case 42: m_gains.voice[5][0] = gain; m_routing.voiceRead[5][0] = address; break;
		case 43: m_gains.voice[5][1] = gain; m_routing.voiceRead[5][1] = address; break;
		case 44: m_gains.reverbReturn[1][5] = gain; m_routing.voiceWrite[5][0] = address; break;
		case 45: m_routing.voiceWrite[5][1] = address; break;
		case 46: m_gains.reverbReturnWithDry0 = gain; m_routing.reverbDrySample[0] = address; break;
		case 47: m_gains.reverbSendSeed0 = gain; break;
		case 48: m_routing.reverbSendSample[0] = address; break;
		case 49: m_gains.reverbMix[0] = gain; m_routing.reverbSendSeedWrite0 = address; break;
		case 50: m_gains.reverbInputFirstMix = gain; m_routing.auxDelayGainSample[0] = address; break;
		case 51: m_routing.reverbMixWrite[0] = address; break;
		case 52: m_gains.voice[6][0] = gain; m_routing.voiceRead[6][0] = address; break;
		case 53: m_gains.voice[6][1] = gain; m_routing.voiceRead[6][1] = address; break;
		case 54: m_routing.voiceWrite[6][0] = address; break;
		case 55: m_routing.voiceWrite[6][1] = address; break;
		case 56: m_gains.reverbReturnGain1 = gain; m_routing.reverbSendSample[1] = address; break;
		case 57: m_gains.reverbInput = gain; break;
		case 58: m_gains.reverbReturnWithSend1 = gain; m_routing.outputSeedSample1 = address; break;
		case 59: m_gains.outputSeed[1] = gain; m_routing.outputSignal[1] = address; break;
		case 60: m_gains.output[1] = gain; break;
		case 61: m_routing.outputSeedWrite[1] = address; break;
		case 62: m_gains.voice[7][0] = gain; m_routing.voiceRead[7][0] = address; break;
		case 63: m_gains.reverbInputHistory0 = gain; m_routing.voiceRead[7][1] = address; break;
		case 64: m_gains.voice[7][1] = gain; m_routing.voiceWrite[7][0] = address; break;
		case 65: m_gains.reverbInputHistoryMix = gain; m_routing.rampIncrementSample[0] = address; break;
		case 66: m_gains.rampIncrement0 = gain; m_routing.voiceWrite[7][1] = address; break;
		case 67: m_gains.reverbFilteredInput = gain; break;
		case 68: m_gains.voice[8][0] = gain; m_routing.voiceRead[8][0] = address; break;
		case 69: m_gains.voice[8][1] = gain; m_routing.voiceRead[8][1] = address; break;
		case 70: m_routing.voiceWrite[8][0] = address; break;
		case 71: m_routing.voiceWrite[8][1] = address; break;
		case 72: m_routing.rampState[0] = address; break;
		case 73: m_gains.rampCandidate[0] = gain; m_routing.reverbDrySample[1] = address; break;
		case 74: m_gains.reverbMix[1] = gain; m_routing.rampLimitSample[0] = address; break;
		case 75: m_gains.rampLimit[0] = gain; m_routing.voiceRead[9][0] = address; break;
		case 76: m_gains.voice[9][0] = gain; m_routing.reverbMixWrite[1] = address; break;
		case 77: m_gains.rampNext[0] = gain; m_routing.rampResetSample[0] = address; break;
		case 78: m_routing.voiceWrite[9][0] = address; break;
		case 79: m_gains.diffuserInput[0] = gain; m_routing.rampNextWrite[0] = address; break;
		case 80: m_gains.voice[9][1] = gain; m_routing.voiceRead[9][1] = address; break;
		case 82: m_gains.diffuserFeedback[0] = gain; m_routing.voiceWrite[9][1] = address; break;
		case 84: m_gains.diffuserInput[1] = gain; break;
		case 85: m_gains.voice[10][0] = gain; m_routing.voiceRead[10][0] = address; break;
		case 86: m_gains.voice[10][1] = gain; m_routing.voiceRead[10][1] = address; break;
		case 87: m_routing.voiceWrite[10][0] = address; break;
		case 88: m_routing.voiceWrite[10][1] = address; break;
		case 89: m_gains.outputSeed[2] = gain; break;
		case 90: m_routing.outputSignal[2] = address; break;
		case 91: m_routing.outputSeedWrite[2] = address; break;
		case 92: m_gains.output[2] = gain; m_routing.outputBias[2] = address; break;
		case 93: m_gains.diffuserFeedback[1] = gain; break;
		case 94: m_gains.voice[11][0] = gain; m_routing.voiceRead[11][0] = address; break;
		case 95: m_gains.diffuserInput[2] = gain; m_routing.voiceRead[11][1] = address; break;
		case 96: m_gains.voice[11][1] = gain; m_routing.voiceWrite[11][0] = address; break;
		case 98: m_gains.diffuserFeedback[2] = gain; m_routing.voiceWrite[11][1] = address; break;
		case 100: m_gains.voice[12][0] = gain; m_routing.voiceRead[12][0] = address; break;
		case 101: m_gains.voice[12][1] = gain; m_routing.voiceRead[12][1] = address; break;
		case 102: m_routing.voiceWrite[12][0] = address; break;
		case 103: m_routing.voiceWrite[12][1] = address; break;
		case 104: m_gains.chorusSendSeed[1] = gain; break;
		case 105: m_gains.reverbFeedbackBase = gain; m_routing.chorusSendSample[1] = address; break;
		case 106: m_gains.chorusSendCapture1 = gain; m_routing.chorusSendSeedWrite[1] = address; break;
		case 107: m_gains.reverbDampingHistory = gain; break;
		case 108: m_routing.chorusSendCaptureWrite1 = address; break;
		case 109: m_gains.previousChorusWithSend[1] = gain; m_routing.previousChorusReturnSample2 = address; break;
		case 110: m_routing.chorusToReverbSample[1] = address; break;
		case 111: m_gains.chorusToReverb[1] = gain; break;
		case 112: m_gains.reverbFeedbackGain = gain; m_routing.voiceRead[13][0] = address; break;
		case 113: m_gains.voice[13][0] = gain; m_routing.chorusToReverbWrite[1] = address; break;
		case 114: m_gains.voice[13][1] = gain; m_routing.voiceRead[13][1] = address; break;
		case 115: m_gains.reverbDampedFeedback = gain; m_routing.voiceWrite[13][0] = address; break;
		case 116: m_routing.voiceWrite[13][1] = address; break;
		case 117: m_gains.reverbTailInput = gain; break;
		case 118: m_gains.voice[14][0] = gain; m_routing.voiceRead[14][0] = address; break;
		case 119: m_gains.voice[14][1] = gain; m_routing.voiceRead[14][1] = address; break;
		case 120: m_gains.tailInput[0] = gain; m_routing.voiceWrite[14][0] = address; break;
		case 121: m_gains.outputSeed[3] = gain; m_routing.voiceWrite[14][1] = address; break;
		case 122: m_routing.outputSignal[3] = address; break;
		case 123: m_gains.tailFeedback[0] = gain; m_routing.outputSeedWrite[3] = address; break;
		case 124: m_gains.output[3] = gain; m_routing.outputBias[3] = address; break;
		case 125: m_gains.voice[15][0] = gain; m_routing.voiceRead[15][0] = address; break;
		case 126: m_gains.voice[15][1] = gain; m_routing.voiceRead[15][1] = address; break;
		case 127: m_routing.voiceWrite[15][0] = address; break;
		case 128: m_routing.voiceWrite[15][1] = address; break;
		case 129: m_routing.rampState[1] = address; break;
		case 130: m_gains.rampCandidate[1] = gain; m_routing.rampIncrementSample[1] = address; break;
		case 131: m_routing.rampLimitSample[1] = address; break;
		case 132: m_gains.rampLimit[1] = gain; break;
		case 133: m_gains.voice[16][0] = gain; m_routing.voiceRead[16][0] = address; break;
		case 134: m_gains.rampNext[1] = gain; m_routing.rampResetSample[1] = address; break;
		case 135: m_routing.voiceWrite[16][0] = address; break;
		case 136: m_routing.rampNextWrite[1] = address; break;
		case 137: m_gains.voice[16][1] = gain; m_routing.voiceRead[16][1] = address; break;
		case 138: m_gains.tailInput[1] = gain; m_routing.rampRangeSample1 = address; break;
		case 139: m_gains.rampRangeCapture1 = gain; m_routing.voiceWrite[16][1] = address; break;
		case 140: m_gains.voice[17][0] = gain; m_routing.voiceRead[17][0] = address; break;
		case 141: m_gains.tailFeedback[1] = gain; m_routing.rampRangeCaptureWrite1 = address; break;
		case 142: m_routing.voiceWrite[17][0] = address; break;
		case 143: m_gains.voice[17][1] = gain; m_routing.voiceRead[17][1] = address; break;
		case 144: m_routing.chorusInputSecondSample = address; break;
		case 145: m_routing.voiceWrite[17][1] = address; break;
		case 146: m_gains.chorusInputMix = gain; m_routing.chorusInputFirstSample = address; break;
		case 147: m_gains.voice[18][0] = gain; m_routing.voiceRead[18][0] = address; break;
		case 148: m_gains.tailInput[2] = gain; m_routing.chorusInputMixWrite = address; break;
		case 149: m_routing.voiceWrite[18][0] = address; break;
		case 150: m_gains.voice[18][1] = gain; m_routing.voiceRead[18][1] = address; break;
		case 152: m_gains.tailFeedback[2] = gain; m_routing.voiceWrite[18][1] = address; break;
		case 153: m_gains.outputSeed[4] = gain; break;
		case 154: m_routing.outputSignal[4] = address; break;
		case 155: m_gains.reverbTailStorage1 = gain; m_routing.outputSeedWrite[4] = address; break;
		case 156: m_gains.output[4] = gain; m_routing.outputBias[4] = address; break;
		case 157: m_gains.voice[19][0] = gain; m_routing.voiceRead[19][0] = address; break;
		case 158: m_gains.voice[19][1] = gain; m_routing.voiceRead[19][1] = address; break;
		case 159: m_routing.voiceWrite[19][0] = address; break;
		case 160: m_routing.voiceWrite[19][1] = address; break;
		case 161: m_routing.chorusFoldSource[0] = address; break;
		case 162: m_gains.phaseFold[0] = gain; m_routing.chorusPhaseBase[0] = address; break;
		case 163: m_routing.voiceRead[20][0] = address; break;
		case 164: m_gains.voice[20][0] = gain; m_routing.chorusPhaseWrite[0] = address; break;
		case 166: m_routing.voiceWrite[20][0] = address; break;
		case 167: m_gains.voice[20][1] = gain; m_routing.voiceRead[20][1] = address; break;
		case 168: m_routing.auxDelayGainSample[1] = address; break;
		case 169: m_routing.voiceWrite[20][1] = address; break;
		case 171: m_gains.voice[21][0] = gain; m_routing.voiceRead[21][0] = address; break;
		case 172: m_gains.voice[21][1] = gain; m_routing.voiceRead[21][1] = address; break;
		case 173: m_routing.voiceWrite[21][0] = address; break;
		case 174: m_routing.voiceWrite[21][1] = address; break;
		case 176: m_gains.auxDelayTransfer2 = gain; m_routing.auxDelaySource2 = address; break;
		case 177: m_routing.chorusPhaseBaseSample1 = address; break;
		case 178: m_gains.chorusPhaseBase1 = gain; break;
		case 179: m_gains.voice[22][0] = gain; m_routing.voiceRead[22][0] = address; break;
		case 180: m_gains.voice[22][1] = gain; m_routing.voiceRead[22][1] = address; break;
		case 181: m_routing.voiceWrite[22][0] = address; break;
		case 182: m_routing.voiceWrite[22][1] = address; break;
		case 183: m_routing.chorusFoldSource[1] = address; break;
		case 184: m_gains.phaseFold[1] = gain; break;
		case 185: m_routing.chorusPhaseOffsetSample[1] = address; break;
		case 186: m_gains.chorusPhase[1] = gain; m_routing.chorusPhaseFoldWrite1 = address; break;
		case 187: m_gains.outputSeed[5] = gain; m_routing.outputSignal[5] = address; break;
		case 188: m_gains.output[5] = gain; m_routing.outputBias[5] = address; break;
		case 189: m_routing.outputSeedWrite[5] = address; break;
		case 191: m_gains.voice[23][0] = gain; m_routing.voiceRead[23][0] = address; break;
		case 192: m_gains.voice[23][1] = gain; m_routing.voiceRead[23][1] = address; break;
		case 193: m_gains.chorusReturnGain[0] = gain; m_routing.voiceWrite[23][0] = address; break;
		case 194: m_routing.voiceWrite[23][1] = address; break;
		case 195: m_gains.voice[24][0] = gain; m_routing.voiceRead[24][0] = address; break;
		case 196: m_gains.voice[24][1] = gain; m_routing.voiceRead[24][1] = address; break;
		case 197: m_routing.voiceWrite[24][0] = address; break;
		case 198: m_gains.chorusPhaseResidual1 = gain; m_routing.voiceWrite[24][1] = address; break;
		case 199: m_routing.chorusInputSample = address; break;
		case 200: m_gains.chorusReturnWithInput0 = gain; m_routing.chorusPhaseResidualWrite1 = address; break;
		case 201: m_gains.chorusInputGain = gain; m_routing.chorusReturnSendSample[0] = address; break;
		case 202: m_gains.chorusReturnMix[0] = gain; break;
		case 203: m_gains.chorusReturnGain[1] = gain; break;
		case 204: m_routing.chorusReturnMixWrite[0] = address; break;
		case 205: m_gains.voice[25][0] = gain; m_routing.voiceRead[25][0] = address; break;
		case 206: m_gains.voice[25][1] = gain; m_routing.voiceRead[25][1] = address; break;
		case 207: m_routing.voiceWrite[25][0] = address; break;
		case 208: m_routing.voiceWrite[25][1] = address; break;
		case 209: m_gains.chorusInputResidual = gain; m_routing.chorusReturnSendSample[1] = address; break;
		case 210: m_gains.chorusReturnWithSend1 = gain; m_routing.chorusCrossfeedSample = address; break;
		case 211: m_gains.chorusDelayInput = gain; m_routing.chorusInputResidualWrite = address; break;
		case 212: m_gains.chorusReturnMix[1] = gain; break;
		case 214: m_routing.chorusReturnMixWrite[1] = address; break;
		case 215: m_gains.voice[26][0] = gain; m_routing.voiceRead[26][0] = address; break;
		case 216: m_gains.voice[26][1] = gain; m_routing.voiceRead[26][1] = address; break;
		case 217: m_routing.voiceWrite[26][0] = address; break;
		case 218: m_routing.voiceWrite[26][1] = address; break;
		case 219: m_gains.outputSeed[6] = gain; m_routing.outputSignal[6] = address; break;
		case 220: m_gains.output[6] = gain; m_routing.outputBias[6] = address; break;
		case 221: m_routing.outputSeedWrite[6] = address; break;
		case 223: m_gains.voice[27][0] = gain; m_routing.voiceRead[27][0] = address; break;
		case 224: m_gains.voice[27][1] = gain; m_routing.voiceRead[27][1] = address; break;
		case 225: m_routing.voiceWrite[27][0] = address; break;
		case 226: m_routing.voiceWrite[27][1] = address; break;
		case 227: m_gains.voice28Base = gain; m_routing.chorusFoldSource[2] = address; break;
		case 228: m_gains.phaseFold[2] = gain; m_routing.chorusPhaseBase[2] = address; break;
		case 229: m_gains.mixBaseUpdate = gain; m_routing.mixBaseUpdateSample = address; break;
		case 230: m_gains.voice[28][0] = gain; break;
		case 231: m_routing.mixBaseUpdateWrite = address; break;
		case 232: m_gains.voice28RoutedSum = gain; m_routing.chorusFoldSource[3] = address; break;
		case 233: m_gains.phaseFold[3] = gain; m_routing.chorusPhaseBase[3] = address; break;
		case 234: m_routing.voice28RoutedSumWrite = address; break;
		case 235: m_gains.chorusPhaseFeedback2 = gain; m_routing.chorusPhaseFeedbackSample2 = address; break;
		case 236: m_gains.chorusPhase[3] = gain; m_routing.chorusPhaseOffsetSample[3] = address; break;
		case 237: m_routing.chorusPhaseFeedbackWrite2 = address; break;
		case 238: m_routing.chorusPhaseWrite[3] = address; break;
		case 239: m_gains.voice[29][0] = gain; m_routing.voiceRead[29][0] = address; break;
		case 240: m_gains.voice[29][1] = gain; m_routing.voiceRead[29][1] = address; break;
		case 241: m_routing.voiceWrite[29][0] = address; break;
		case 242: m_routing.voiceWrite[29][1] = address; break;
		case 245: m_gains.voice[30][0] = gain; m_routing.voiceRead[30][0] = address; break;
		case 246: m_gains.voice[30][1] = gain; m_routing.voiceRead[30][1] = address; break;
		case 247: m_routing.voiceWrite[30][0] = address; break;
		case 248: m_routing.voiceWrite[30][1] = address; break;
		case 249: m_gains.outputSeed[7] = gain; m_routing.outputSignal[7] = address; break;
		case 251: m_routing.outputSeedWrite[7] = address; break;
		case 252: m_gains.output[7] = gain; m_routing.outputBias[7] = address; break;
		case 254: m_gains.voice[31][0] = gain; m_routing.voiceRead[31][0] = address; break;
		case 255: m_gains.voice[31][1] = gain; m_routing.voiceRead[31][1] = address; break;
		}
	}}

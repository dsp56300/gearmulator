#include "dspMultiTI.h"

namespace virusLib
{
	DspMultiTI::DspMultiTI() : DspSingle(0x100000, true, "Master"), m_dsp2(0x100000, true, "Slave "), m_buffer(4)
	{
		getHDI08().writeHDR(0x0000);			// this = Master
		m_dsp2.getHDI08().writeHDR(0x8000);		// m_dsp = Slave

		m_dsp2.getPeriphX().getEsai().writeEmptyAudioIn(4);
	}

	void DspMultiTI::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples, uint32_t _latency)
	{
		const auto s = static_cast<uint32_t>(_samples);

		const float* inputs[] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		float* outputs[] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

		inputs[0] = _inputs[0];
		inputs[1] = _inputs[1];

		getPeriphX().getEsai().processAudioInputInterleaved(inputs, s, _latency);

		inputs[0] = nullptr;
		inputs[1] = nullptr;
		getPeriphY().getEsai().processAudioInputInterleaved(inputs, s * 3, _latency);

		m_dsp2.getPeriphX().getEsai().processAudioInputInterleaved(inputs, s, _latency);
		m_dsp2.getPeriphY().getEsai().processAudioInputInterleaved(inputs, s * 3, _latency);

		// ESAI outputs

		// DAC outputs on the master are fed by ESAI in regular fashion
		outputs[4] = _outputs[1];		outputs[5] = _outputs[0];
		outputs[6] = _outputs[3];		outputs[7] = _outputs[2];
		outputs[8] = _outputs[5];		outputs[9] = _outputs[4];

		getPeriphX().getEsai().processAudioOutputInterleaved(outputs, s, _latency);

		// USB outputs on the Slave are sent in regular fashion via ESAI
		outputs[4] = _outputs[7];		outputs[5] = _outputs[6];
		outputs[6] = _outputs[9];		outputs[7] = _outputs[8];
		outputs[8] = _outputs[11];		outputs[9] = _outputs[10];

		m_dsp2.getPeriphX().getEsai().processAudioOutputInterleaved(outputs, s, _latency);

		// ESAI_1 outputs
		m_buffer.ensureSize(s*3);

		float* outputsMix[] = {m_buffer.getChannel(0), m_buffer.getChannel(1), m_buffer.getChannel(2), m_buffer.getChannel(3), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

		// USB outputs on the Master are sent to the Slave via ESAI_1 in 1/3 interleaved format => unpack it
		getPeriphY().getEsai().processAudioOutputInterleaved(outputsMix, s * 3, _latency);

		mixEsai1Output(_outputs[6] , _outputs[7] , outputsMix[3], outputsMix[2], s, 2, 1);	// USB 1
		mixEsai1Output(_outputs[8] , _outputs[9] , outputsMix[0], outputsMix[1], s, 2, 0);	// USB 2
		mixEsai1Output(_outputs[10], _outputs[11], outputsMix[2], outputsMix[3], s, 2, 0);	// USB 3

		// DAC outputs on the Slave are send via ESAI_1 to the Master in 1/3 interleaved format => unpack it
		m_dsp2.getPeriphY().getEsai().processAudioOutputInterleaved(outputsMix, s*3, _latency);

		mixEsai1Output(_outputs[0], _outputs[1], outputsMix[0], outputsMix[1], s, 1, 2);	// Out 1
		mixEsai1Output(_outputs[2], _outputs[3], outputsMix[2], outputsMix[3], s, 1, 2);	// Out 2
		mixEsai1Output(_outputs[4], _outputs[5], outputsMix[1], outputsMix[0], s, 0, 2);	// Out 3
	}

	void DspMultiTI::mixEsai1Output(float* _dstL, float* _dstR, const float* _srcL, const float* _srcR, uint32_t _samples, uint32_t _srcOffsetL, uint32_t _srcOffsetR)
	{
		if(!_dstL || !_dstR)
			return;

		uint32_t iSrcL = _srcOffsetL;
		uint32_t iSrcR = _srcOffsetR;

		for(uint32_t i=0; i<_samples; ++i, iSrcL += 3, iSrcR += 3)
		{
			_dstL[i] += _srcL[iSrcL];
			_dstR[i] += _srcR[iSrcR];
		}
	}
}

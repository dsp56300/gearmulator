#include "dspMultiTI.h"

namespace virusLib
{
	DspMultiTI::DspMultiTI() : DspSingle(0x100000, true, "Master"), m_dsp2(0x100000, true, "Slave ")
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

		outputs[4] = _outputs[0];
		outputs[5] = _outputs[1];

		getPeriphX().getEsai().processAudioOutputInterleaved(outputs, s, _latency);

		outputs[4] = nullptr;
		outputs[5] = nullptr;

		getPeriphY().getEsai().processAudioOutputInterleaved(outputs, s * 3, _latency);

		m_dsp2.getPeriphX().getEsai().processAudioOutputInterleaved(outputs, s, _latency);
		m_dsp2.getPeriphY().getEsai().processAudioOutputInterleaved(outputs, s*3, _latency);
	}
}

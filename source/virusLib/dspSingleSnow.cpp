#include "dspSingleSnow.h"

#if VIRUS_SUPPORT_TI

namespace virusLib
{
	DspSingleSnow::DspSingleSnow() : DspSingle(0x100000, true)
	{
	}

	template<typename T> void
	processAudioSnow(DspSingleSnow& _dsp, const synthLib::TAudioInputsT<T>& _inputs, const synthLib::TAudioOutputsT<T>& _outputs, const size_t _samples, const uint32_t _latency)
	{
		const auto s = static_cast<uint32_t>(_samples);

		const T* inputs0[] = {_inputs[1], _inputs[0], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		const T* inputs1[] = {_inputs[3], _inputs[2], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

		_dsp.getPeriphX().getEsai().processAudioInputInterleaved(inputs0, s, _latency);
		_dsp.getPeriphY().getEsai().processAudioInputInterleaved(inputs1, s, _latency);

		T* outputs0[] = {_outputs[1], _outputs[0], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		T* outputs1[] = {nullptr, nullptr, _outputs[3], _outputs[2], _outputs[5], _outputs[4], _outputs[7], _outputs[6], nullptr, nullptr, nullptr, nullptr};

		_dsp.getPeriphX().getEsai().processAudioOutputInterleaved(outputs0, s);
		_dsp.getPeriphY().getEsai().processAudioOutputInterleaved(outputs1, s);
	}

	void DspSingleSnow::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _samples, const uint32_t _latency)
	{
		processAudioSnow(*this, _inputs, _outputs, _samples, _latency);
	}

	void DspSingleSnow::processAudio(const synthLib::TAudioInputsInt& _inputs, const synthLib::TAudioOutputsInt& _outputs, const size_t _samples, const uint32_t _latency)
	{
		processAudioSnow(*this, _inputs, _outputs, _samples, _latency);
	}
}

#endif
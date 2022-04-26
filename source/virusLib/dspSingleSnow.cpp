#include "dspSingleSnow.h"

namespace virusLib
{
	DspSingleSnow::DspSingleSnow() : DspSingle(0x100000, true)
	{
	}

	void DspSingleSnow::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples, uint32_t _latency)
	{
		const auto s = static_cast<uint32_t>(_samples);

		const float* inputs0[] = {_inputs[1], _inputs[0], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		const float* inputs1[] = {_inputs[3], _inputs[2], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

		getPeriphX().getEsai().processAudioInputInterleaved(inputs0, s, _latency);
		getPeriphY().getEsai().processAudioInputInterleaved(inputs1, s, _latency);

		float* outputs0[] = {_outputs[1], _outputs[0], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		float* outputs1[] = {nullptr, nullptr, _outputs[3], _outputs[2], _outputs[5], _outputs[4], _outputs[7], _outputs[6], nullptr, nullptr, nullptr, nullptr};

		getPeriphX().getEsai().processAudioOutputInterleaved(outputs0, s, _latency);
		getPeriphY().getEsai().processAudioOutputInterleaved(outputs1, s, _latency);
	}
}

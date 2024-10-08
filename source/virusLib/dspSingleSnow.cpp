#include "dspSingleSnow.h"

namespace virusLib
{
	DspSingleSnow::DspSingleSnow() : DspSingle(0x100000, true)
	{
	}

	template<typename T> void
	processAudioSnow(DspSingleSnow& _dsp, const synthLib::TAudioInputsT<T>& _inputs, const synthLib::TAudioOutputsT<T>& _outputs, const size_t _samples, const uint32_t _latency, std::vector<T>& _dummyIn, std::vector<T>& _dummyOut)
	{
		DspSingle::ensureSize(_dummyIn, _samples<<1);
		DspSingle::ensureSize(_dummyOut, _samples<<1);

		const auto* dIn = _dummyIn.data();
		auto* dOut = _dummyOut.data();

		const auto s = static_cast<uint32_t>(_samples);

		const T* inputs0[] = {_inputs[0], _inputs[1], dIn, dIn, dIn, dIn, dIn, dIn};
		const T* inputs1[] = {_inputs[0], _inputs[1], dIn, dIn, dIn, dIn, dIn, dIn};

		_dsp.getPeriphX().getEsai().processAudioInputInterleaved(inputs0, s, _latency);
		_dsp.getPeriphY().getEsai().processAudioInputInterleaved(inputs1, s, _latency);

		T* outputs0[] = {
			_outputs[0] ? _outputs[0] : dOut,
			_outputs[1] ? _outputs[1] : dOut,
			dOut, dOut, dOut, dOut, dOut, dOut, dOut, dOut, dOut, dOut};

		T* outputs1[] = {dOut, dOut, dOut, dOut,
			_outputs[2] ? _outputs[2] : dOut,
			_outputs[3] ? _outputs[3] : dOut,
			_outputs[4] ? _outputs[4] : dOut,
			_outputs[5] ? _outputs[5] : dOut,
			_outputs[6] ? _outputs[6] : dOut,
			_outputs[7] ? _outputs[7] : dOut,
			dOut, dOut};

		_dsp.getPeriphX().getEsai().processAudioOutputInterleaved(outputs0, s);
		_dsp.getPeriphY().getEsai().processAudioOutputInterleaved(outputs1, s);
	}

	void DspSingleSnow::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _samples, const uint32_t _latency)
	{
		processAudioSnow(*this, _inputs, _outputs, _samples, _latency, m_dummyBufferInF, m_dummyBufferOutF);
	}

	void DspSingleSnow::processAudio(const synthLib::TAudioInputsInt& _inputs, const synthLib::TAudioOutputsInt& _outputs, const size_t _samples, const uint32_t _latency)
	{
		processAudioSnow(*this, _inputs, _outputs, _samples, _latency, m_dummyBufferInI, m_dummyBufferOutI);
	}
}

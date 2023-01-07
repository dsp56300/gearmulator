#include "dspMultiTI.h"

#if VIRUS_SUPPORT_TI

namespace virusLib
{
	DspMultiTI::DspMultiTI() : DspSingle(0x100000, true, "Master"), m_dsp2(0x100000, true, "Slave ")
	{
		m_bufferF.resize(4);
		m_bufferI.resize(4);

		getHDI08().writeHDR(0x0000);			// this = Master
		m_dsp2.getHDI08().writeHDR(0x8000);		// m_dsp = Slave

		getPeriphX().getEsai().writeEmptyAudioIn(1);
		m_dsp2.getPeriphX().getEsai().writeEmptyAudioIn(3);
	}

	void audioAdd(float& _dst, float _src)
	{
		_dst += _src;
	}

	void audioAdd(dsp56k::TWord& _dst, dsp56k::TWord _src)
	{
		auto dst = dsp56k::signextend<int32_t, 24>(static_cast<int32_t>(_dst));
		dst += dsp56k::signextend<int32_t, 24>(static_cast<int32_t>(_src));
		_dst = static_cast<dsp56k::TWord>(dst) & 0xffffff;
	}

	template<typename T>
	void mixEsai1Output(T* _dstL, T* _dstR, const T* _srcL, const T* _srcR, uint32_t _samples, uint32_t _srcOffsetL, uint32_t _srcOffsetR)
	{
		if(!_dstL || !_dstR)
			return;

		uint32_t iSrcL = _srcOffsetL;
		uint32_t iSrcR = _srcOffsetR;

		for(uint32_t i=0; i<_samples; ++i, iSrcL += 3, iSrcR += 3)
		{
			audioAdd(_dstL[i], _srcL[iSrcL]);
			audioAdd(_dstR[i], _srcR[iSrcR]);
		}
	}

	template<typename T>
	void createEsai1Input(T* _dstL, T* _dstR, const T* _srcL, const T* _srcR, uint32_t _samples, uint32_t _dstOffsetL, uint32_t _dstOffsetR)
	{
		if(!_dstL || !_dstR)
			return;

		uint32_t iDstL = _dstOffsetL;
		uint32_t iDstR = _dstOffsetR;

		for(uint32_t i=0; i<_samples; ++i, iDstL += 3, iDstR += 3)
		{
			_dstL[iDstL  ]   = _srcL[i];
			_dstR[iDstR  ]   = _srcR[i];
		}
	}

	template<typename T>
	void processAudioTI(DspMultiTI& _dsp, DspSingle& _dsp2, std::vector<std::vector<T>>& _buffer, const synthLib::TAudioInputsT<T>& _inputs, const synthLib::TAudioOutputsT<T>& _outputs, const size_t _samples, const uint32_t _latency)
	{
		const auto s = static_cast<uint32_t>(_samples);

		if(_buffer[0].size() < s*3)
		{
			for (auto& buffer : _buffer)
				buffer.resize(s*3);
		}

		const T* inputs[] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		T* outputs[] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

		inputs[0] = nullptr;
		inputs[1] = nullptr;
		_dsp.getPeriphX().getEsai().processAudioInputInterleaved(inputs, s, _latency);

		createEsai1Input(&_buffer[0][0], &_buffer[1][0], &_inputs[0][0], &_inputs[1][0], s, 1, 2);
		inputs[0] = nullptr;
		inputs[1] = nullptr;
		inputs[2] = &_buffer[0][0];
		inputs[3] = &_buffer[1][0];
		inputs[4] = nullptr;
		inputs[5] = nullptr;
		_dsp.getPeriphY().getEsai().processAudioInputInterleaved(inputs, s * 3, _latency * 3);

		inputs[0] = nullptr;
		inputs[1] = nullptr;
		_dsp2.getPeriphX().getEsai().processAudioInputInterleaved(inputs, s, _latency);

		inputs[0] = nullptr;
		inputs[1] = nullptr;
		_dsp2.getPeriphY().getEsai().processAudioInputInterleaved(inputs, s * 3, _latency * 3);

		// ESAI outputs

		// DAC outputs on the master are fed by ESAI in regular fashion
		outputs[4] = _outputs[1];		outputs[5] = _outputs[0];
		outputs[6] = _outputs[3];		outputs[7] = _outputs[2];
		outputs[8] = _outputs[5];		outputs[9] = _outputs[4];

		_dsp.getPeriphX().getEsai().processAudioOutputInterleaved(outputs, s);

		// USB outputs on the Slave are sent in regular fashion via ESAI
		outputs[4] = _outputs[7];		outputs[5] = _outputs[6];
		outputs[6] = _outputs[9];		outputs[7] = _outputs[8];
		outputs[8] = _outputs[11];		outputs[9] = _outputs[10];

		_dsp2.getPeriphX().getEsai().processAudioOutputInterleaved(outputs, s);

		// ESAI_1 outputs

		T* outputsMix[] = {&_buffer[0][0], &_buffer[1][0], &_buffer[2][0], &_buffer[3][0], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

		// USB outputs on the Master are sent to the Slave via ESAI_1 in 1/3 interleaved format => unpack it
		_dsp.getPeriphY().getEsai().processAudioOutputInterleaved(outputsMix, s * 3);

		mixEsai1Output(_outputs[6] , _outputs[7] , outputsMix[3], outputsMix[2], s, 2, 1);	// USB 1
		mixEsai1Output(_outputs[8] , _outputs[9] , outputsMix[0], outputsMix[1], s, 2, 0);	// USB 2
		mixEsai1Output(_outputs[10], _outputs[11], outputsMix[2], outputsMix[3], s, 2, 0);	// USB 3

		// DAC outputs on the Slave are send via ESAI_1 to the Master in 1/3 interleaved format => unpack it
		_dsp2.getPeriphY().getEsai().processAudioOutputInterleaved(outputsMix, s*3);

		mixEsai1Output(_outputs[0], _outputs[1], outputsMix[0], outputsMix[1], s, 1, 2);	// Out 1
		mixEsai1Output(_outputs[2], _outputs[3], outputsMix[2], outputsMix[3], s, 1, 2);	// Out 2
		mixEsai1Output(_outputs[4], _outputs[5], outputsMix[1], outputsMix[0], s, 0, 2);	// Out 3
	}

	void DspMultiTI::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples, uint32_t _latency)
	{
		processAudioTI(*this, m_dsp2, m_bufferF, _inputs, _outputs, _samples, _latency);
	}

	void DspMultiTI::processAudio(const synthLib::TAudioInputsInt& _inputs, const synthLib::TAudioOutputsInt& _outputs, size_t _samples, uint32_t _latency)
	{
		processAudioTI(*this, m_dsp2, m_bufferI, _inputs, _outputs, _samples, _latency);
	}
}

#endif
#include "dspMultiTI.h"

#if VIRUS_SUPPORT_TI

DSP56K_DEOPTIMIZE

constexpr uint32_t g_esai1Padding = 3;

namespace virusLib
{
	DspMultiTI::DspMultiTI() : DspSingle(0x100000, true, "Master"), m_dsp2(0x100000, true, "Slave ")
	{
		getHDI08().writeHDR(0x0000);			// this = Master
		m_dsp2.getHDI08().writeHDR(0x8000);		// m_dsp2 = Slave

		getPeriphX().getEsai().writeEmptyAudioIn(1);
		m_dsp2.getPeriphX().getEsai().writeEmptyAudioIn(1);
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
	void mixEsai1Output(T* _dstL, T* _dstR, const T* _srcL, const T* _srcR, uint32_t _samples, int32_t _srcOffsetL, int32_t _srcOffsetR)
	{
		if(!_dstL || !_dstR)
			return;

		_srcL += _srcOffsetL;
		_srcR += _srcOffsetR;

		for(uint32_t i=0; i<_samples; ++i, _srcL += 3, _srcR += 3)
		{
			audioAdd(_dstL[i], *_srcL);
			audioAdd(_dstR[i], *_srcR);
		}
	}

	template<typename T>
	void createEsai1Input(T* _dstL, T* _dstR, const T* _srcL, const T* _srcR, uint32_t _samples, int32_t _dstOffsetL, int32_t _dstOffsetR)
	{
		if(!_dstL || !_dstR)
			return;

		int32_t iDstL = _dstOffsetL;
		int32_t iDstR = _dstOffsetR;

		for(uint32_t i=0; i<_samples; ++i, iDstL += 3, iDstR += 3)
		{
			_dstL[iDstL  ]   = _srcL[i];
			_dstR[iDstR  ]   = _srcR[i];
		}
	}

	template <typename T, size_t Size> void ensureSize(DspMultiTI::EsaiBuf<T, Size>& _buf, size_t _size)
	{
		if (_buf[0].size() < _size)
		{
			for (auto &buffer : _buf)
				buffer.resize(_size, 0);
		}
	}

	template <typename T> void ensureSize(DspMultiTI::EsaiBufs<T> &_bufs, size_t _size)
	{
		ensureSize(_bufs.input, _size);
		ensureSize(_bufs.dspAout, _size);
		ensureSize(_bufs.dspBout, _size);
	}

	template <typename T> void wrapPadding(std::vector<T>& _buf, const size_t _usedSize)
	{
		size_t iSrc = _usedSize;

		for (size_t i=0; i<g_esai1Padding; ++i, ++iSrc)
			_buf[i] = _buf[iSrc];
	}

	template <typename T, size_t Size> void wrapPadding(DspMultiTI::EsaiBuf<T, Size>& _buf, size_t _usedSize)
	{
		for (size_t i=0; i<_buf.size(); ++i)
			wrapPadding(_buf[i], _usedSize);
	}

	template <typename T> void wrapPadding(DspMultiTI::EsaiBufs<T>& _bufs, size_t _usedSize)
	{
		wrapPadding(_bufs.input, _usedSize);
		wrapPadding(_bufs.dspAout, _usedSize);
		wrapPadding(_bufs.dspBout, _usedSize);
	}

	template <typename T>
	void processAudioTI(DspMultiTI& _dsp, DspSingle& _dsp2, DspMultiTI::EsaiBufs<T>& _buffers, const synthLib::TAudioInputsT<T>& _inputs, const synthLib::TAudioOutputsT<T>& _outputs, const size_t _samples, const uint32_t _latency)
	{
		const auto s = static_cast<uint32_t>(_samples);

		ensureSize(_buffers, s * 3 + g_esai1Padding);

		// ESAI inputs
		const T* inputs[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

		// Master ESAI input might be the USB input, we don't need it as we only have one input
		_dsp.getPeriphX().getEsai().processAudioInputInterleaved(inputs, s, _latency);

		createEsai1Input(&_buffers.input[0][g_esai1Padding], &_buffers.input[1][g_esai1Padding], &_inputs[1][0], &_inputs[0][0], s, 0, -2);

		// Master ESAI_1 input gets the analog input from the slave, inject the interleaved input here
		inputs[2] = &_buffers.input[0][0];
		inputs[3] = &_buffers.input[1][0];
		_dsp.getPeriphY().getEsai().processAudioInputInterleaved(inputs, s * 3, _latency * 3);
		inputs[2] = nullptr;
		inputs[3] = nullptr;

		// Slave ESAI input gets the analog input in regular fashion
		// TODO: phase issue, one channel is offset by 1 sample
		inputs[0] = &_inputs[0][0];
		inputs[1] = &_inputs[1][0];
		_dsp2.getPeriphX().getEsai().processAudioInputInterleaved(inputs, s, _latency);
		inputs[0] = nullptr;
		inputs[1] = nullptr;

		// Slave ESAI_1 does not get the ADC at all but only data from the master, we don't need it here
		_dsp2.getPeriphY().getEsai().processAudioInputInterleaved(inputs, s * 3, _latency * 3);

		// ESAI outputs
		T* outputs[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

		// DAC outputs on the master are fed by ESAI in regular fashion
		outputs[4] = _outputs[0];		outputs[5] = _outputs[1];
		outputs[6] = _outputs[2];		outputs[7] = _outputs[3];
		outputs[8] = _outputs[4];		outputs[9] = _outputs[5];

		_dsp.getPeriphX().getEsai().processAudioOutputInterleaved(outputs, s);

		// USB outputs on the Slave are sent in regular fashion via ESAI
		outputs[4] = _outputs[6];		outputs[5] = _outputs[7];
		outputs[6] = _outputs[8];		outputs[7] = _outputs[9];
		outputs[8] = _outputs[10];		outputs[9] = _outputs[11];

		_dsp2.getPeriphX().getEsai().processAudioOutputInterleaved(outputs, s);

		// ESAI_1 outputs

		T* outputsMixA[] = {&_buffers.dspAout[0][g_esai1Padding], &_buffers.dspAout[1][g_esai1Padding], &_buffers.dspAout[2][g_esai1Padding], &_buffers.dspAout[3][g_esai1Padding], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		T* outputsMixB[] = {&_buffers.dspBout[0][g_esai1Padding], &_buffers.dspBout[1][g_esai1Padding], &_buffers.dspBout[2][g_esai1Padding], &_buffers.dspBout[3][g_esai1Padding], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

		// USB outputs on the Master are sent to the Slave via ESAI_1 in 1/3 interleaved format => unpack it
		_dsp.getPeriphY().getEsai().processAudioOutputInterleaved(outputsMixA, s * 3);
#if 1
		mixEsai1Output(_outputs[6] , _outputs[7] , outputsMixA[3], outputsMixA[2], s, -2, 0);	// USB 1
		mixEsai1Output(_outputs[8] , _outputs[9] , outputsMixA[0], outputsMixA[1], s, 1, -1);	// USB 2
		mixEsai1Output(_outputs[10], _outputs[11], outputsMixA[2], outputsMixA[3], s, 1, -1);	// USB 3
#endif
		// DAC outputs on the Slave are send via ESAI_1 to the Master in 1/3 interleaved format => unpack it
		_dsp2.getPeriphY().getEsai().processAudioOutputInterleaved(outputsMixB, s * 3);
#if 1
		mixEsai1Output(_outputs[0], _outputs[1], outputsMixB[1], outputsMixB[0], s, -2, 0); // Out 1
		mixEsai1Output(_outputs[2], _outputs[3], outputsMixB[3], outputsMixB[2], s, -2, 0); // Out 2
		mixEsai1Output(_outputs[4], _outputs[5], outputsMixB[0], outputsMixB[1], s, 1, -1); // Out 3
#endif
		wrapPadding(_buffers, s*3);
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
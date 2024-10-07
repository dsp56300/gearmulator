#include "dspMultiTI.h"

namespace virusLib
{
	constexpr uint32_t g_esai1TxBlockSize = 6 * 3 * 2;		// 6 = number of TX pins, 3 = number of slots per frame, 2 = double data rate
	constexpr uint32_t g_esai1TxPadding = g_esai1TxBlockSize;

	constexpr uint32_t g_esai1RxBlockSize = 4 * 3 * 2;		// 4 = number of RX pins, 3 = number of slots per frame, 2 = double data rate
	constexpr uint32_t g_esai1RxPadding = g_esai1RxBlockSize;

	constexpr uint32_t g_magicIntervalSamples = 256;

	static constexpr dsp56k::TWord g_magicNumber	= 0xedc987;
	
	void audioAdd(float& _dst, float _src)
	{
		_dst += _src;
	}

	void audioAdd(dsp56k::TWord& _dst, const dsp56k::TWord _src)
	{
		auto dst = dsp56k::signextend<int32_t, 24>(static_cast<int32_t>(_dst));
		dst += dsp56k::signextend<int32_t, 24>(static_cast<int32_t>(_src));
		_dst = static_cast<dsp56k::TWord>(dst) & 0xffffff;
	}

	template <typename T> void ensureSize(std::vector<T>& _buf, size_t _size)
	{
		if(_buf.size() > _size)
			return;
		_buf.resize(_size, 0);
	}

	template <typename T> void wrapPadding(std::vector<T>& _buf, const uint32_t _usedSize)
	{
		size_t iSrc = _usedSize;

		for (size_t i=0; i<g_esai1TxPadding; ++i, ++iSrc)
			_buf[i] = _buf[iSrc];
	}

	template <typename T> void DspMultiTI::Esai1Out::processAudioOutput(dsp56k::Esai& _esai, uint32_t _frames, const synthLib::TAudioOutputsT<T>& _outputs, uint32_t _firstOutChannel, const std::array<uint32_t, 6>& _sourceIndices)
	{
		ensureSize(*this, _frames * g_esai1TxBlockSize + g_esai1TxPadding);

		_esai.processAudioOutput(data() + g_esai1TxPadding, _frames * 2);

		if(m_blockStart == InvalidOffset)
		{
			for(uint32_t i=g_esai1TxPadding; i<g_esai1TxBlockSize * _frames + g_esai1TxPadding; ++i)
			{
				if(at(i) != g_magicNumber)
					continue;

				// OS writes the magic value to position $b of its internal ring buffer
				const auto off = i - 0xb;

				m_blockStart = off / g_esai1TxBlockSize;	
				m_blockStart = off - m_blockStart * g_esai1TxBlockSize;
				break;
			}
		}

		if(m_blockStart != InvalidOffset)
		{
			const auto* p = &at(m_blockStart);

			for(size_t i=0; i<_frames; ++i)
			{
				_outputs[_firstOutChannel  ][i] += dsp56k::dsp2sample<T>(p[_sourceIndices[0]]);
				_outputs[_firstOutChannel+1][i] += dsp56k::dsp2sample<T>(p[_sourceIndices[1]]);
				_outputs[_firstOutChannel+2][i] += dsp56k::dsp2sample<T>(p[_sourceIndices[2]]);
				_outputs[_firstOutChannel+3][i] += dsp56k::dsp2sample<T>(p[_sourceIndices[3]]);
				_outputs[_firstOutChannel+4][i] += dsp56k::dsp2sample<T>(p[_sourceIndices[4]]);
				_outputs[_firstOutChannel+5][i] += dsp56k::dsp2sample<T>(p[_sourceIndices[5]]);

				p += g_esai1TxBlockSize;
			}
		}

		wrapPadding(*this, _frames * g_esai1TxBlockSize);
	}

	template <typename T> void DspMultiTI::Esai1in::processAudioinput(dsp56k::Esai& _esai, uint32_t _frames, uint32_t _latency, const synthLib::TAudioInputsT<T>& _inputs)
	{
		ensureSize(*this, _frames * g_esai1RxBlockSize);

		uint32_t blockIdx = 0;

		static volatile uint32_t offset = 1;

		for(uint32_t i=0; i<_frames; ++i)
		{
			at(blockIdx + offset + (g_esai1RxBlockSize>>1)) = dsp56k::sample2dsp<T>(_inputs[0][i]);
			at(blockIdx + offset                          ) = dsp56k::sample2dsp<T>(_inputs[1][i]);

			blockIdx += g_esai1RxBlockSize;
		}

		_esai.processAudioInput(data(), _frames * 2, 3, _latency * 2);
	}

	DspMultiTI::DspMultiTI() : DspSingle(0x100000, true, "DSP A"), m_dsp2(0x100000, true, "DSP B")
	{
		getHDI08().writeHDR(0x0000);			// this = Master
		m_dsp2.getHDI08().writeHDR(0x8000);		// m_dsp2 = Slave

		getPeriphX().getEsai().writeEmptyAudioIn(2);
		m_dsp2.getPeriphX().getEsai().writeEmptyAudioIn(2);
	}

	template <typename T>
	void processAudioTI(DspMultiTI& _dsp, DspSingle& _dsp2, DspMultiTI::EsaiBufs<T>& _buffers, const synthLib::TAudioInputsT<T>& _inputs, synthLib::TAudioOutputsT<T> _outputs, const size_t _samples, const uint32_t _latency)
	{
		const auto s = static_cast<uint32_t>(_samples);

		// ESAI inputs
		ensureSize(_buffers.dummyInput, _samples << 1);
		const T* in = _buffers.dummyInput.data();
		const T* inputs[8] = { in, in, in, in, in, in, in, in };

		// Master ESAI input might be the USB input, we don't need it as we only have one input
		_dsp.getPeriphX().getEsai().processAudioInputInterleaved(inputs, s, _latency);

		// Master ESAI_1 input gets the analog input from the slave, inject the interleaved input here
		_buffers.in.processAudioinput(_dsp.getPeriphY().getEsai(), s, _latency, _inputs);

		// Slave ESAI input gets the analog input in regular fashion

		_dsp2.getPeriphX().getEsai().processAudioInput<T>(1, _latency, [&](size_t _s, dsp56k::Audio::RxFrame& _frame)
		{
			_frame.resize(2);
			_frame[0][0] = dsp56k::sample2dsp<T>(_inputs[0][0]);
			_frame[1][0] = dsp56k::sample2dsp<T>(_buffers.m_previousInput);
		});

		_dsp2.getPeriphX().getEsai().processAudioInput<T>(s - 1, _latency, [&](size_t _s, dsp56k::Audio::RxFrame& _frame)
		{
			_frame.resize(2);
			_frame[0][0] = dsp56k::sample2dsp<T>(_inputs[0][_s]);
			_frame[1][0] = dsp56k::sample2dsp<T>(_inputs[1][_s+1]);
		});

		_buffers.m_previousInput = _inputs[1][s-1];

		// Slave ESAI_1 does not get the ADC at all but only data from the master, we don't need it here
		_dsp2.getPeriphY().getEsai().processAudioInputInterleaved(inputs, s * 2, _latency * 2);

		// ESAI outputs
		ensureSize(_buffers.dummyOutput, _samples << 1);
		for (auto& o : _outputs)
		{
			if(!o)
				o = _buffers.dummyOutput.data();
		}

		T* out = _buffers.dummyOutput.data();
		T* outputs[12] = {out, out, out, out, out, out, out, out, out, out, out, out};

		// DAC outputs on the Master are sent in regular fashion
		outputs[4] = _outputs[0];		outputs[5] = _outputs[1];
		outputs[6] = _outputs[2];		outputs[7] = _outputs[3];
		outputs[8] = _outputs[4];		outputs[9] = _outputs[5];

		_dsp.getPeriphX().getEsai().processAudioOutputInterleaved(outputs, s);

		// USB outputs on the Slave are sent in regular fashion
		outputs[4] = _outputs[6];		outputs[5] = _outputs[7];
		outputs[6] = _outputs[8];		outputs[7] = _outputs[9];
		outputs[8] = _outputs[10];		outputs[9] = _outputs[11];

		_dsp2.getPeriphX().getEsai().processAudioOutputInterleaved(outputs, s);

		// ESAI_1 outputs
		constexpr auto halfBS = g_esai1TxBlockSize >> 1;

		// USB outputs on the Master are sent to the Slave via ESAI_1 in 1/3 interleaved format => unpack it
		_buffers.dspA.processAudioOutput(_dsp.getPeriphY().getEsai(), s, _outputs, 6, {
			5, 5+halfBS,
			16+halfBS, 16,
			17+halfBS, 17
		});

		// DAC outputs on the Slave are send via ESAI_1 to the Master in 1/3 interleaved format => unpack it
		_buffers.dspB.processAudioOutput(_dsp2.getPeriphY().getEsai(), s, _outputs, 0, {
			4,4+halfBS,
			5,5+halfBS,
			16+halfBS,16
		});
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

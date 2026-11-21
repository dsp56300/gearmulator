#include "dspMultiTI.h"

#include <iterator>

#include "dsp56kBase/logging.h"

#include "dsp56kEmu/dsp.h"

namespace virusLib
{
	constexpr uint32_t g_esai1TxBlockSize = 6 * 3 * 2;		// 6 = number of TX pins, 3 = number of slots per frame, 2 = double data rate
	constexpr uint32_t g_esai1TxPadding = g_esai1TxBlockSize;

	constexpr uint32_t g_esai1RxBlockSize = 4 * 3 * 2;		// 4 = number of RX pins, 3 = number of slots per frame, 2 = double data rate
	constexpr uint32_t g_esai1RxPadding = g_esai1RxBlockSize;

	constexpr uint32_t g_magicIntervalSamples = 256;

	static constexpr dsp56k::TWord g_magicNumber	= 0xedc987;

	// ESAI_1 frames have three slots, the OS of one DSP talks to the other one in the second
	constexpr uint32_t g_esai1Slots = 3;
	constexpr uint32_t g_controlSlot = 1;

	// A DSP only takes part when the marker of the other one arrives in its ring of 512 frames where it sent its own, so
	// the way there and back has to take whole rings. On the chip the wire takes no time, but a DSP thread here can only
	// take a frame that the other one finished, so the master gets what the slave sends as on the chip and the slave
	// what the master sends one ring later. The master copies the clock of the slave, which stays as on the chip
	constexpr uint32_t g_controlFramesToSlave = 512;
	constexpr uint32_t g_controlFramesToMaster = 0;

	// a DSP that waits for the other one runs again once that many frames arrived
	constexpr uint32_t g_controlWakeFrames = 64;
	
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

		{
			// The slave moves its ring once it finds the marker of the master (EMU-240), by whole frames, so the marker
			// stays in the same channel. Only that one counts once the block start is known, the others carry audio
			const auto* const buf = data();

			for(uint32_t i=g_esai1TxPadding; i<g_esai1TxBlockSize * _frames + g_esai1TxPadding; ++i)
			{
				if(buf[i] != g_magicNumber)
					continue;

				constexpr uint32_t frameSize = g_esai1TxBlockSize >> 1;

				if(m_blockStart != InvalidOffset && (i - 0xb + frameSize - m_blockStart) % frameSize)
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

		// The OS reads the analog input from RX1 of slot 1, L in the first frame of a sample, R in the second. Until
		// dsp56300 8ae1681e, a transmitter that the OS switched on while another one was already running (TE0, then
		// TE1) added a slot to the transmit frame, and the OS picked the input up from slot 0 instead.
		constexpr uint32_t offset = 5;

		auto* const rx = data();
		constexpr uint32_t half = g_esai1RxBlockSize >> 1;

		for(uint32_t i=0; i<_frames; ++i)
		{
			rx[blockIdx + offset      ] = dsp56k::sample2dsp<T>(_inputs[0][i]);
			rx[blockIdx + offset + half] = dsp56k::sample2dsp<T>(_inputs[1][i]);

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

		m_toSlave.prefill(g_controlFramesToSlave);
		m_toMaster.prefill(g_controlFramesToMaster);

		connect(*this, m_toMaster, m_toSlave);
		connect(m_dsp2, m_toSlave, m_toMaster);
	}

	DspMultiTI::~DspMultiTI()
	{
		stopDSPThread();
	}

	void DspMultiTI::stopDSPThread()
	{
		// each DSP waits for the other one, both have to stop before either goes away
		m_toSlave.close();
		m_toMaster.close();

		DspSingle::stopDSPThread();
		m_dsp2.stopDSPThread();
	}

	bool DspMultiTI::disableNoteHandover(DspSingle& _dsp)
	{
		// The OS hands a note-on to the other DSP when its load is above $280000 and above the load the other DSP
		// reported, (r5) plays it here: move #$28,x0 / cmp x0,a x:>otherLoad,x0 / jle (r5) / cmp x0,a / jle (r5),
		// at P:$053cfe in TI and TI2 OS 5.0.7 and 5.1.7. The other DSP starts the note once it arrived over ESAI_1,
		// about 11 ms late on the chip, so the notes of the host jitter with a patch that keeps a DSP busy. Each DSP
		// plays its notes itself, as before the DSPs were connected (EMU-240): the second jle becomes a jmp
		constexpr dsp56k::TWord sig[] = {0x242800, 0x44f045, 0, 0x0ae5af, 0x200045, 0x0ae5af};
		constexpr dsp56k::TWord jmpR5 = 0x0ae580;

		auto& mem = _dsp.getMemory();
		const auto* p = mem.getMemAreaPtr(dsp56k::MemArea_P);

		for(dsp56k::TWord i=0; i + std::size(sig) <= mem.sizeP(); ++i)
		{
			if(p[i] != sig[0] || p[i+1] != sig[1] || p[i+3] != sig[3] || p[i+4] != sig[4] || p[i+5] != sig[5])
				continue;

			const auto addr = i + 5;

			// the JIT may run that code right now
			_dsp.runOnDspThread([&dsp = _dsp.getDSP(), addr]
			{
				dsp.memWriteP(addr, jmpR5);
			});

			LOG("Note handover to the other DSP disabled at P:$" << HEX(addr));
			return true;
		}

		LOG("Note handover not found in this OS, a busy DSP keeps handing notes to the other one");
		return false;
	}

	void DspMultiTI::connect(DspSingle& _dsp, ControlLink& _in, ControlLink& _out)
	{
		auto& esai = _dsp.getPeriphX().getEsai();
		auto& esai1 = _dsp.getPeriphY().getEsai();

		// everything the DSP sends still goes to processAudioTI, the control slot also goes to the other DSP
		esai1.setWriteTxCallback([&esai1, &_out](uint64_t& _frameIndex, const dsp56k::Audio::TxFrame& _frame)
		{
			esai1.getAudioOutputs().push_back(_frame);

			if(_frame.size() > g_controlSlot)
				_out.push({_frame[g_controlSlot][0], _frame[g_controlSlot][1]});
			else
				_out.push({});

			++_frameIndex;
		});

		// The emulated ESAI raises the receive interrupt of a slot after the transmit ones, the chip before them. In the
		// last slot the frame interrupt of the OS moves its ring pointer in between, so the OS takes what arrives in a
		// slot for the slot after it: what the other DSP sent in the control slot has to arrive one slot earlier
		esai1.setReadRxCallback([&esai1, &_in, &_out](uint64_t& _frameIndex, dsp56k::Audio::RxFrame& _frame)
		{
			auto& frames = esai1.getAudioInputs();

			if(frames.empty())
				_out.producerWaits();

			_frame = frames.pop_front();

			for(auto s = _frame.size(); s < g_esai1Slots; ++s)
			{
				_frame.resize(s + 1);
				_frame[s] = {};
			}

			if(_in.empty())
				_out.producerWaits();

			const auto words = _in.pop();

			_frame[g_controlSlot - 1][0] = words[0];
			_frame[g_controlSlot - 1][1] = words[1];

			++_frameIndex;
		});

		// the same as the default, but the other DSP must not wait for this one while it waits for audio
		esai.setReadRxCallback([&esai, &_out](uint64_t& _frameIndex, dsp56k::Audio::RxFrame& _frame)
		{
			auto& frames = esai.getAudioInputs();

			if(frames.empty())
				_out.producerWaits();

			_frame = frames.pop_front();
			++_frameIndex;
		});
	}

	void DspMultiTI::ControlLink::prefill(const uint32_t _count)
	{
		for(uint32_t i=0; i<_count; ++i)
			m_words.push_back({});
	}

	void DspMultiTI::ControlLink::push(const Words& _words)
	{
		// only while the OS of the receiving DSP did not switch its receiver on yet
		if(m_words.full())
		{
			if(!m_overflow)
				LOG("ESAI_1 control link overflow, the other DSP does not receive");
			m_overflow = true;
			return;
		}

		m_words.push_back(_words);
		m_producerWaits.store(false, std::memory_order_relaxed);

		// A receiver that waits does not pop, so the link grows by one per push and passes the number of frames that
		// wake it exactly once. Only that push has to look for it, the others save the fence
		if(m_words.size() == g_controlWakeFrames)
			wakeReceiver(g_controlWakeFrames);
	}

	void DspMultiTI::ControlLink::producerWaits()
	{
		m_producerWaits.store(true, std::memory_order_relaxed);

		wakeReceiver(1);
	}

	void DspMultiTI::ControlLink::wakeReceiver(const size_t _minWords)
	{
		// pairs with the fence in pop(): either the receiver sees what was written before, or this sees it waiting
		std::atomic_thread_fence(std::memory_order_seq_cst);

		if(!m_receiverWaits.load(std::memory_order_relaxed))
			return;

		std::lock_guard lock(m_mutex);

		if(m_words.size() >= _minWords)
			m_cv.notify_one();
	}

	DspMultiTI::ControlLink::Words DspMultiTI::ControlLink::pop()
	{
		if(m_words.empty())
		{
			std::unique_lock lock(m_mutex);

			m_receiverWaits.store(true, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_seq_cst);

			// Waking a thread for every frame costs more than the emulation of the frame. Wait for a few, or for less
			// when the other DSP is about to wait itself, for audio or for this one
			m_cv.wait(lock, [this]
			{
				const auto size = m_words.size();
				return m_closed || size >= g_controlWakeFrames || (size && m_producerWaits.load(std::memory_order_relaxed));
			});

			m_receiverWaits.store(false, std::memory_order_relaxed);

			if(m_words.empty())
				return {};
		}

		return m_words.pop_front();
	}

	void DspMultiTI::ControlLink::close()
	{
		std::lock_guard lock(m_mutex);
		m_closed = true;
		m_cv.notify_all();
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

		// Slave ESAI input gets the analog input. The OS pairs the right sample of a frame with the left sample of the
		// frame after it, so every frame carries the left sample of the sample before
		_dsp2.getPeriphX().getEsai().processAudioInput<T>(s, _latency, [&](size_t _s, dsp56k::Audio::RxFrame& _frame)
		{
			_frame.resize(2);
			_frame[0][0] = dsp56k::sample2dsp<T>(_s ? _inputs[0][_s-1] : _buffers.m_previousInput);
			_frame[1][0] = dsp56k::sample2dsp<T>(_inputs[1][_s]);
		});

		_buffers.m_previousInput = _inputs[0][s-1];

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

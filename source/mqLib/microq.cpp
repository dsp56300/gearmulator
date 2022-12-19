#include "microq.h"

#include "../synthLib/midiTypes.h"
#include "../synthLib/os.h"

#include "mqhardware.h"

namespace mqLib
{
	MicroQ::MicroQ(BootMode _bootMode/* = BootMode::Default*/)
	{
		// create hardware, will use in-memory ROM if no ROM provided
		auto romFile = synthLib::findROM(512 * 1024);
		if(romFile.empty())
			romFile = synthLib::findFile(".mid", 300 * 1024, 400 * 1024);

		m_hw.reset(new Hardware(romFile));

		if(_bootMode != BootMode::Default)
			m_hw->setBootMode(_bootMode);

		m_midiInBuffer.reserve(1024);
		m_midiOutBuffer.reserve(1024);

		m_ucThread.reset(new std::thread([&]()
		{
			dsp56k::DSPThread::setCurrentThreadName("MC68331");
			while(!m_destroy)
				processUcThread();
			m_destroy = false;
			m_hw->ucThreadTerminated();
		}));

		m_hw->getUC().getLeds().setChangeCallback([this]()
		{
			onLedsChanged();
		});

		m_hw->getUC().getLcd().setChangeCallback([this]()
		{
			onLcdChanged();
		});
	}

	MicroQ::~MicroQ()
	{
		// we need to have passed the boot stage
		m_hw->processAudio(1);

		m_destroy = true;

		// DSP needs to run to let the uc thread wake up
		const auto& esai = m_hw->getDSP().getPeriph().getEsai();
		while(m_destroy)
		{
			if(!esai.getAudioOutputs().empty())
				m_hw->processAudio(1);
			else
				std::this_thread::yield();
		}

		m_ucThread->join();
		m_ucThread.reset();
		m_hw.reset();
	}

	bool MicroQ::isValid() const
	{
		return m_hw && m_hw->isValid();
	}

	void MicroQ::process(const float** _inputs, float** _outputs, uint32_t _frames, uint32_t _latency)
	{
		std::lock_guard lock(m_mutex);

		m_hw->ensureBufferSize(_frames);

		// convert inputs from float to DSP words
		auto& dspIns = m_hw->getAudioInputs();

		for(size_t c=0; c<dspIns.size(); ++c)
		{
			for(uint32_t i=0; i<_frames; ++i)
				dspIns[c][i] = dsp56k::sample2dsp(_inputs[c][i]);
		}

		internalProcess(_frames, _latency);

		// convert outputs from DSP words to float
		const auto& dspOuts = m_hw->getAudioOutputs();

		for(size_t c=0; c<dspOuts.size(); ++c)
		{
			for(uint32_t i=0; i<_frames; ++i)
				_outputs[c][i] = dsp56k::dsp2sample<float>(dspOuts[c][i]);
		}
	}

	void MicroQ::process(uint32_t _frames, uint32_t _latency)
	{
		std::lock_guard lock(m_mutex);
		internalProcess(_frames, _latency);
	}

	void MicroQ::internalProcess(uint32_t _frames, uint32_t _latency)
	{
		// send midi in
		m_hw->sendMidi(m_midiInBuffer);
		m_midiInBuffer.clear();

		// process audio
		m_hw->processAudio(_frames, _latency);

		// receive midi output
		m_hw->receiveMidi(m_midiOutBuffer);
	}

	TAudioInputs& MicroQ::getAudioInputs()
	{
		return m_hw->getAudioInputs();
	}

	TAudioOutputs& MicroQ::getAudioOutputs()
	{
		return m_hw->getAudioOutputs();
	}

	void MicroQ::sendMidi(const uint8_t _byte)
	{
		std::lock_guard lock(m_mutex);
		m_midiInBuffer.push_back(_byte);
	}

	void MicroQ::sendMidi(const uint8_t _a, const uint8_t _b)
	{
		std::lock_guard lock(m_mutex);
		m_midiInBuffer.push_back(_a);
		m_midiInBuffer.push_back(_b);
	}

	void MicroQ::sendMidi(const uint8_t _a, const uint8_t _b, const uint8_t _c)
	{
		std::lock_guard lock(m_mutex);
		m_midiInBuffer.push_back(_a);
		m_midiInBuffer.push_back(_b);
		m_midiInBuffer.push_back(_c);
	}

	void MicroQ::sendMidi(const std::vector<uint8_t>& _buffer)
	{
		std::lock_guard lock(m_mutex);
		m_midiInBuffer.insert(m_midiInBuffer.end(), _buffer.begin(), _buffer.end());
	}

	void MicroQ::sendMidiEvent(const synthLib::SMidiEvent& _ev)
	{
		std::lock_guard lock(m_mutex);

		if(!_ev.sysex.empty())
		{
			m_midiInBuffer.insert(m_midiInBuffer.end(), _ev.sysex.begin(), _ev.sysex.end());
		}
		else
		{
			m_midiInBuffer.push_back(_ev.a);

			const auto command = _ev.a & 0xf0;

			if(command != 0xf0)
			{
				if(command == synthLib::M_AFTERTOUCH)
				{
					m_midiInBuffer.push_back(_ev.b);
				}
				else
				{
					m_midiInBuffer.push_back(_ev.b);
					m_midiInBuffer.push_back(_ev.c);
				}
			}
		}
	}

	void MicroQ::receiveMidi(std::vector<uint8_t>& _buffer)
	{
		std::lock_guard lock(m_mutex);
		std::swap(_buffer, m_midiOutBuffer);
		m_midiOutBuffer.clear();
	}

	bool MicroQ::getButton(Buttons::ButtonType _button)
	{
		return m_hw->getUC().getButtons().getButtonState(_button) != 0;
	}

	void MicroQ::setButton(Buttons::ButtonType _button, bool _pressed)
	{
		m_hw->getUC().getButtons().setButton(_button, _pressed);
	}

	uint8_t MicroQ::getEncoder(Buttons::Encoders _encoder)
	{
		return m_hw->getUC().getButtons().getEncoderState(_encoder);
	}

	void MicroQ::rotateEncoder(Buttons::Encoders _encoder, int _amount)
	{
		std::lock_guard lock(m_mutex);
		m_hw->getUC().getButtons().rotate(_encoder, _amount);
	}

	bool MicroQ::getLedState(Leds::Led _led)
	{
		return m_hw->getUC().getLeds().getLedState(_led) != 0;
	}

	void MicroQ::readLCD(std::array<char, 40>& _data)
	{
		_data = m_hw->getUC().getLcd().getDdRam();
	}

	bool MicroQ::readCustomLCDCharacter(std::array<uint8_t, 8>& _data, uint32_t _characterIndex)
	{
		std::lock_guard lock(m_mutex);
		return m_hw->getUC().getLcd().getCgData(_data, _characterIndex);
	}

	MicroQ::DirtyFlags MicroQ::getDirtyFlags()
	{
		const auto f = m_dirtyFlags.exchange(0);
		return static_cast<DirtyFlags>(f);
	}

	Hardware* MicroQ::getHardware()
	{
		return m_hw.get();
	}

	bool MicroQ::isBootCompleted() const
	{
		return m_hw && m_hw->isBootCompleted();
	}

	void MicroQ::onLedsChanged()
	{
		m_dirtyFlags.fetch_or(static_cast<uint32_t>(DirtyFlags::Leds));
	}

	void MicroQ::onLcdChanged()
	{
		m_dirtyFlags.fetch_or(static_cast<uint32_t>(DirtyFlags::Lcd));
	}

	void MicroQ::processUcThread() const
	{
		for(size_t i=0; i<32; ++i)
		{
			m_hw->process();
			m_hw->process();
			m_hw->process();
			m_hw->process();
			m_hw->process();
			m_hw->process();
			m_hw->process();
			m_hw->process();
		}
	}
}

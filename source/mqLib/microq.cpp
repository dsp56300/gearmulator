#include "microq.h"

#include "../synthLib/midiTypes.h"
#include "../synthLib/os.h"

#include "mqhardware.h"

namespace mqLib
{
	MicroQ::MicroQ()
	{
		// load ROM
		const auto romFile = synthLib::findROM(512 * 1024);

		if(romFile.empty())
			throw std::runtime_error("Failed to find ROM. Copy ROM file next to executable.");

		// create hardware
		m_hw.reset(new Hardware(romFile));

		m_midiInBuffer.reserve(1024);
		m_midiOutBuffer.reserve(1024);

		m_ucThread.reset(new std::thread([&]()
		{
			while(!m_destroy)
				processUcThread();
		}));
	}

	MicroQ::~MicroQ()
	{
		m_destroy = true;
		m_ucThread->join();
	}

	void MicroQ::process(const float** _inputs, float** _outputs, uint32_t _frames)
	{
		std::lock_guard lock(m_mutex);

		// send midi in

		for(size_t i=0; i<m_midiInBuffer.size(); ++i)
			m_hw->sendMidi(m_midiInBuffer[i]);
		m_midiInBuffer.clear();

		// process audio

		m_hw->ensureBufferSize(_frames);

		// convert inputs from float to DSP words
		auto& dspIns = m_hw->getAudioInputs();

		for(size_t c=0; c<dspIns.size(); ++c)
		{
			for(uint32_t i=0; i<_frames; ++i)
				dspIns[c][i] = dsp56k::sample2dsp(_inputs[c][i]);
		}

		m_hw->processAudio(_frames);

		// convert outputs from DSP words to float
		const auto& dspOuts = m_hw->getAudioOutputs();

		for(size_t c=0; c<dspOuts.size(); ++c)
		{
			for(uint32_t i=0; i<_frames; ++i)
				_outputs[c][i] = dsp56k::dsp2sample<float>(dspOuts[c][i]);
		}

		// receive midi output
		m_hw->receiveMidi(m_midiOutBuffer);
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

	void MicroQ::setButton(Buttons::ButtonType _button, bool _pressed)
	{
		std::lock_guard lock(m_mutex);
		m_hw->getUC().getButtons().setButton(_button, _pressed);
	}

	void MicroQ::rotateEncoder(Buttons::Encoders _encoder, int _amount)
	{
		std::lock_guard lock(m_mutex);
		m_hw->getUC().getButtons().rotate(_encoder, _amount);
	}

	bool MicroQ::getLedState(Leds::Led _led)
	{
		std::lock_guard lock(m_mutex);
		return m_hw->getUC().getLeds().getLedState(_led) != 0;
	}

	void MicroQ::readLCD(std::array<char, 40>& _data)
	{
		std::lock_guard lock(m_mutex);
		_data = m_hw->getUC().getLcd().getDdRam();
	}

	bool MicroQ::readCustomLCDCharacter(std::array<uint8_t, 8>& _data, uint32_t _characterIndex)
	{
		std::lock_guard lock(m_mutex);
		return m_hw->getUC().getLcd().getCgData(_data, _characterIndex);
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

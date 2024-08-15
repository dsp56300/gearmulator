#include "n2xdevice.h"

#include "n2xhardware.h"
#include "n2xtypes.h"

namespace n2x
{
	Device::Device() : m_state(&m_hardware)
	{
	}

	const std::string& Device::getRomFilename() const
	{
		return m_hardware.getRomFilename();
	}

	float Device::getSamplerate() const
	{
		return g_samplerate;
	}

	bool Device::isValid() const
	{
		return m_hardware.isValid();
	}

	bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return m_state.getState(_state);
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return m_state.setState(_state);
	}

	uint32_t Device::getChannelCountIn()
	{
		return 0;
	}

	uint32_t Device::getChannelCountOut()
	{
		return 4;
	}

	bool Device::setDspClockPercent(const uint32_t _percent)
	{
		bool res = m_hardware.getDSPA().getPeriph().getEsaiClock().setSpeedPercent(_percent);
		res &= m_hardware.getDSPB().getPeriph().getEsaiClock().setSpeedPercent(_percent);
		return res;
	}

	uint32_t Device::getDspClockPercent() const
	{
		return const_cast<Hardware&>(m_hardware).getDSPA().getPeriph().getEsaiClock().getSpeedPercent();
	}

	uint64_t Device::getDspClockHz() const
	{
		return const_cast<Hardware&>(m_hardware).getDSPA().getPeriph().getEsaiClock().getSpeedInHz();
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		m_hardware.getMidi().read(m_midiOutBuffer);
		m_midiParser.write(m_midiOutBuffer);
		m_midiOutBuffer.clear();
		m_midiParser.getEvents(_midiOut);

		for (const auto& midiOut : _midiOut)
		{
			if(midiOut.sysex.empty())
				LOG("TX " << HEXN(midiOut.a,2) << ' ' << HEXN(midiOut.b,2) << ' ' << HEXN(midiOut.c, 2));
			else
				LOG("TX Sysex of size " << midiOut.sysex.size());
			m_state.receive(midiOut);
		}
	}

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples)
	{
		m_hardware.processAudio(_outputs, static_cast<uint32_t>(_samples), getExtraLatencySamples());
		m_numSamplesProcessed += static_cast<uint32_t>(_samples);

//		m_hardware.setButtonState(ButtonType::OscSync, (m_numSamplesProcessed & 65535) > 2048);
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		if(_ev.sysex.empty())
		{
			// drop program change messages. We do not have any valid presets in the device, this will select garbage
			if((_ev.a & 0xf0) == synthLib::M_PROGRAMCHANGE)
				return true;

			m_state.receive(_response, _ev);
			auto e = _ev;
			e.offset += m_numSamplesProcessed + getExtraLatencySamples();
			m_hardware.sendMidi(e);
		}
		else
		{
			if(m_state.receive(_response, _ev))
				return true;

			m_hardware.sendMidi(_ev);
		}
		return true;
	}
}

#include "n2xdevice.h"

#include "n2xhardware.h"
#include "n2xtypes.h"

namespace n2x
{
	Device::Device()
	{
		m_hardware.reset(new Hardware());
	}

	float Device::getSamplerate() const
	{
		return g_samplerate;
	}

	bool Device::isValid() const
	{
		return m_hardware->isValid();
	}

	bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		// TODO
		return false;
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		// TODO
		return false;
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
		bool res = m_hardware->getDSPA().getPeriph().getEsaiClock().setSpeedPercent(_percent);
		res &= m_hardware->getDSPB().getPeriph().getEsaiClock().setSpeedPercent(_percent);
		return res;
	}

	uint32_t Device::getDspClockPercent() const
	{
		return m_hardware->getDSPA().getPeriph().getEsaiClock().getSpeedPercent();
	}

	uint64_t Device::getDspClockHz() const
	{
		return m_hardware->getDSPA().getPeriph().getEsaiClock().getSpeedInHz();
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		m_hardware->getMidi().read(m_midiOutBuffer);
		m_midiParser.write(m_midiOutBuffer);
		m_midiOutBuffer.clear();
		m_midiParser.getEvents(_midiOut);
	}

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples)
	{
		m_hardware->processAudio(_outputs, static_cast<uint32_t>(_samples), getExtraLatencySamples());
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		return m_hardware->sendMidi(_ev);
	}
}

#include "device.h"

namespace mqLib
{
	Device::Device() : m_mq(BootMode::Default)
	{
		while(!m_mq.isBootCompleted())
			m_mq.process(8);
	}

	Device::~Device() = default;

	uint32_t Device::getInternalLatencyMidiToOutput() const
	{
		return 0;
	}

	uint32_t Device::getInternalLatencyInputToOutput() const
	{
		return 0;
	}

	float Device::getSamplerate() const
	{
		return 44100.0f;
	}

	bool Device::isValid() const
	{
		return m_mq.isValid();
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
		return 2;
	}

	uint32_t Device::getChannelCountOut()
	{
		return 6;
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		m_mq.receiveMidi(m_midiOutBuffer);
		m_midiOutParser.write(m_midiOutBuffer);
		m_midiOutParser.getEvents(_midiOut);
		m_midiOutBuffer.clear();
	}

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		const float* inputs[2] = {_inputs[0], _inputs[1]};
		float* outputs[6] = {_outputs[0], _outputs[1], _outputs[2], _outputs[3], _outputs[4], _outputs[5]};

		m_mq.process(inputs, outputs, static_cast<uint32_t>(_samples));
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		m_mq.sendMidiEvent(_ev);
		return true;
	}
}

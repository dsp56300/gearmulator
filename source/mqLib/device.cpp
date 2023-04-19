#include "device.h"

#include "mqmiditypes.h"

#include <cstring>

#include "../synthLib/deviceTypes.h"

#include "mqbuildconfig.h"
#include "mqhardware.h"

namespace mqLib
{
	Device::Device() : m_mq(BootMode::Default), m_state(m_mq), m_sysexRemote(m_mq)
	{
		// we need to hit the play button to resume boot if the used rom is an OS update. mQ will complain about an uninitialized ROM area in this case
		m_mq.setButton(Buttons::ButtonType::Play, true);
		while(!m_mq.isBootCompleted())
			m_mq.process(8);
		m_mq.setButton(Buttons::ButtonType::Play, false);

		m_state.createInitState();

		auto* hw = m_mq.getHardware();
		hw->resetMidiCounter();
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
		return m_state.getState(_state, _type);
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		if constexpr (g_pluginDemo)
			return false;

		return m_state.setState(_state, _type);
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

		Responses responses;

		for (const auto& midiOut : _midiOut)
		{
			m_state.receive(responses, midiOut, State::Origin::Device);

			for (auto& response : responses)
			{
				auto& r = _midiOut.emplace_back();
				std::swap(response, r.sysex);
			}
		}

		if(!m_customSysexOut.empty())
		{
			_midiOut.insert(_midiOut.begin(), m_customSysexOut.begin(), m_customSysexOut.end());
			m_customSysexOut.clear();
		}
	}

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		const float* inputs[2] = {_inputs[0], _inputs[1]};
		float* outputs[6] = {_outputs[0], _outputs[1], _outputs[2], _outputs[3], _outputs[4], _outputs[5]};

		m_mq.process(inputs, outputs, static_cast<uint32_t>(_samples), getExtraLatencySamples());

		const auto dirty = static_cast<uint32_t>(m_mq.getDirtyFlags());

		m_sysexRemote.handleDirtyFlags(m_customSysexOut, dirty);
	}

	void Device::process(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _size, const std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		synthLib::Device::process(_inputs, _outputs, _size, _midiIn, _midiOut);

		m_numSamplesProcessed += static_cast<uint32_t>(_size);
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		const auto& sysex = _ev.sysex;

		if (!sysex.empty())
		{
			if (m_sysexRemote.receive(m_customSysexOut, sysex))
				return true;
		}

		Responses responses;

		const auto res = m_state.receive(responses, _ev, State::Origin::External);

		for (auto& response : responses)
		{
			auto& r = _response.emplace_back();
			std::swap(response, r.sysex);
		}

		// do not forward to device if our cache was able to reply. It might have sent something to the device already on its own if a cache miss occured
		if(res)
			return true;

		if(_ev.sysex.empty())
		{
			auto e = _ev;
			e.offset += m_numSamplesProcessed + getExtraLatencySamples();
			m_mq.sendMidiEvent(e);
		}
		else
		{
			m_mq.sendMidiEvent(_ev);
		}


		return true;
	}

}

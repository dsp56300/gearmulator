#include "device.h"

#include "mqmiditypes.h"

#include <cstring>

#include "../synthLib/deviceTypes.h"

#include "mqbuildconfig.h"

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

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		const auto& sysex = _ev.sysex;

		if(!sysex.empty())
		{
			if(m_sysexRemote.receive(m_customSysexOut, sysex))
				return true;

			Responses responses;
			auto res = m_state.receive(responses, sysex, State::Origin::External);

			for (auto& response : responses)
			{
				auto& r = _response.emplace_back();
				std::swap(response, r.sysex);
			}

			// do not forward to device if our cache as able to reply. It might have sent something to the device already on its own if a cache miss occured
			if(res)
				return true;
		}

		m_mq.sendMidiEvent(_ev);

		return true;
	}

	void Device::createSequencerMultiData(std::vector<uint8_t>& _data)
	{
		static_assert(
			(static_cast<uint32_t>(MultiParameter::Inst15) - static_cast<uint32_t>(MultiParameter::Inst0)) == 
			(static_cast<uint32_t>(MultiParameter::Inst1 ) - static_cast<uint32_t>(MultiParameter::Inst0)) * 15, 
			"we need a consecutive offset");

		_data.assign(static_cast<uint32_t>(mqLib::MultiParameter::Count), 0);

		constexpr char name[] = "Emu-Plugin-Multi";
		static_assert(std::size(name) == 17, "wrong name length");
		memcpy(&_data[static_cast<uint32_t>(MultiParameter::Name00)], name, sizeof(name) - 1);

		auto setParam = [&](MultiParameter _param, uint8_t _value)
		{
			_data[static_cast<uint32_t>(_param)] = _value;
		};

		auto setInstParam = [&](const uint8_t _instIndex, const MultiParameter _param, const uint8_t _value)
		{
			auto index = static_cast<uint32_t>(MultiParameter::Inst0) + (static_cast<uint32_t>(MultiParameter::Inst1) - static_cast<uint32_t>(MultiParameter::Inst0)) * _instIndex;
			index += static_cast<uint32_t>(_param) - static_cast<uint32_t>(MultiParameter::Inst0);
			_data[index] = _value;
		};

		setParam(MultiParameter::Volume, 127);						// max volume

		setParam(MultiParameter::ControlW, 120);					// global
		setParam(MultiParameter::ControlX, 120);					// global
		setParam(MultiParameter::ControlY, 120);					// global
		setParam(MultiParameter::ControlZ, 120);					// global

		for(uint8_t i=0; i<16; ++i)
		{
			setInstParam(i, MultiParameter::Inst0SoundBank, 0);	    // bank A
			setInstParam(i, MultiParameter::Inst0SoundBank, i);	    // sound number i
			setInstParam(i, MultiParameter::Inst0MidiChannel, i);	// midi channel i
			setInstParam(i, MultiParameter::Inst0Volume, 127);		// max volume
			setInstParam(i, MultiParameter::Inst0Transpose, 64);	// no transpose
			setInstParam(i, MultiParameter::Inst0Detune, 64);		// no detune
			setInstParam(i, MultiParameter::Inst0Output, 0);		// main out
			setInstParam(i, MultiParameter::Inst0Flags, 3);			// RX = Local+MIDI / TX = off / Engine = Play
			setInstParam(i, MultiParameter::Inst0Pan, 64);			// center
			setInstParam(i, MultiParameter::Inst0Pattern, 0);		// no pattern
			setInstParam(i, MultiParameter::Inst0VeloLow, 0);		// full velocity range
			setInstParam(i, MultiParameter::Inst0VeloHigh, 127);
			setInstParam(i, MultiParameter::Inst0KeyLow, 0);		// full key range
			setInstParam(i, MultiParameter::Inst0KeyHigh, 127);
			setInstParam(i, MultiParameter::Inst0MidiRxFlags, 63);	// enable Pitchbend, Modwheel, Aftertouch, Sustain, Button 1/2, Program Change
		}
	}
}

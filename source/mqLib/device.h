#pragma once

#include "microq.h"
#include "mqstate.h"
#include "mqsysexremotecontrol.h"

#include "../synthLib/device.h"
#include "../synthLib/midiBufferParser.h"

namespace mqLib
{
	class Device : public synthLib::Device
	{
	public:
		Device();
		~Device() override;
		uint32_t getInternalLatencyMidiToOutput() const override;
		uint32_t getInternalLatencyInputToOutput() const override;
		float getSamplerate() const override;
		bool isValid() const override;
		bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		uint32_t getChannelCountIn() override;
		uint32_t getChannelCountOut() override;

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

	private:
		MicroQ						m_mq;
		State						m_state;
		SysexRemoteControl			m_sysexRemote;
		std::vector<uint8_t>		m_midiOutBuffer;
		synthLib::MidiBufferParser	m_midiOutParser;
		std::vector<synthLib::SMidiEvent> m_customSysexOut;
	};
}

#pragma once

#include "microq.h"

#include "../synthLib/device.h"
#include "../synthLib/midiBufferParser.h"

namespace mqLib
{
	enum class SysexCommand : uint8_t;

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

		static void createSequencerMultiData(std::vector<uint8_t>& _data);

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

		static void createSysexHeader(std::vector<uint8_t>& _dst, SysexCommand _cmd);

		void sendSysexLCD(std::vector<synthLib::SMidiEvent>& _dst);
		void sendSysexButtons(std::vector<synthLib::SMidiEvent>& _dst);
		void sendSysexLEDs(std::vector<synthLib::SMidiEvent>& _dst);
		void sendSysexRotaries(std::vector<synthLib::SMidiEvent>& _dst);

	private:
		MicroQ						m_mq;
		std::vector<uint8_t>		m_midiOutBuffer;
		synthLib::MidiBufferParser	m_midiOutParser;
		std::vector<synthLib::SMidiEvent> m_customSysexOut;
	};
}

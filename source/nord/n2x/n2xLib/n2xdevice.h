#pragma once

#include "n2xhardware.h"
#include "n2xstate.h"

#include "wLib/wDevice.h"

namespace n2x
{
	class Hardware;

	class Device : public synthLib::Device
	{
	public:
		Device(const synthLib::DeviceCreateParams& _params);

		float getSamplerate() const override;
		bool isValid() const override;
		bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		uint32_t getChannelCountIn() override;
		uint32_t getChannelCountOut() override;
		bool setDspClockPercent(uint32_t _percent) override;
		uint32_t getDspClockPercent() const override;
		uint64_t getDspClockHz() const override;

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

	private:
		Hardware m_hardware;
		State m_state;
		std::vector<uint8_t> m_midiOutBuffer;
		synthLib::MidiBufferParser m_midiParser;
		uint32_t m_numSamplesProcessed = 0;
	};
}

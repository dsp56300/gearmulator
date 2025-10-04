#pragma once

#include <deque>
#include <memory>

#include "state.h"
#include "synthLib/device.h"

namespace jeLib
{
	class Je8086;
}

namespace jeLib
{
	class Device : public synthLib::Device
	{
	public:
		Device(const synthLib::DeviceCreateParams& _params);
		Device(const Device&) = delete;
		Device& operator=(const Device&) = delete;
		Device(Device&&) = delete;
		Device& operator=(Device&&) = delete;
		~Device() override;

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

		std::unique_ptr<Je8086> m_je8086;
		std::deque<synthLib::SMidiEvent> m_midiIn;
		State m_state;
	};
}

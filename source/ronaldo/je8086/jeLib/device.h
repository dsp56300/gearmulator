#pragma once

#include <memory>

#include "state.h"
#include "sysexRemoteControl.h"

#include "synthLib/device.h"

namespace jeLib
{
	class JeThread;
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

		void onParamChanged(uint8_t _page, uint8_t _index, int32_t _value);

		void createMasterVolumeMessage(std::vector<synthLib::SMidiEvent>& _messages) const;

		std::unique_ptr<Je8086> m_je8086;
		std::unique_ptr<JeThread> m_thread;

		std::vector<synthLib::SMidiEvent> m_midiIn;
		std::vector<synthLib::SMidiEvent> m_midiOut;

		State m_state;
		SysexRemoteControl m_sysexRemote;

		baseLib::EventListener<uint8_t, uint8_t, int32_t> m_paramChangedListener;

		float m_masterVolume = 3.0f;
	};
}

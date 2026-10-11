#pragma once

#include <memory>

#include "state.h"

#include "synthLib/device.h"
#include "synthLib/frontPanelSysex.h"

namespace rLib
{
	template<typename TBoard> class BoardThread;
}

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
		uint64_t getDspClockHz() const override;

		uint32_t getInternalLatencyMidiToOutput() const override;

		void setMasterVolume(float _volume)
		{
			m_masterVolume = _volume;
		}

		Je8086& getJe8086() const
		{
			return *m_je8086;
		}

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

		void onParamChanged(uint8_t _page, uint8_t _index, int32_t _value);

		void createMasterVolumeMessage(std::vector<synthLib::SMidiEvent>& _messages) const;

		std::unique_ptr<Je8086> m_je8086;
		std::unique_ptr<rLib::BoardThread<Je8086>> m_thread;

		std::vector<synthLib::SMidiEvent> m_midiIn;
		std::vector<synthLib::SMidiEvent> m_midiOut;

		State m_state;
		synthLib::FrontPanelTransport m_frontPanel;

		baseLib::EventListener<uint8_t, uint8_t, int32_t> m_paramChangedListener;

		float m_masterVolume = 12.0f;
	};
}

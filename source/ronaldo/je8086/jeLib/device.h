#pragma once

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "state.h"

#include "synthLib/device.h"
#include "synthLib/frontPanelSysex.h"

namespace synthLib
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
		void onTransportDiscontinuity(const synthLib::SMidiEvent& _ev) override;

		void onParamChanged(uint8_t _page, uint8_t _index, int32_t _value);

		void createCustomParameterMessages(std::vector<synthLib::SMidiEvent>& _messages) const;

		std::unique_ptr<Je8086> m_je8086;
		std::unique_ptr<synthLib::BoardThread<Je8086>> m_thread;

		std::vector<synthLib::SMidiEvent> m_midiIn;
		std::vector<synthLib::SMidiEvent> m_midiOut;

		State m_state;
		synthLib::FrontPanelTransport m_frontPanel;

		// the other model's system block, never applied here, saved so that switching the ROM back restores it
		synthLib::SysexBuffer m_otherModelSystemDump;

		baseLib::EventListener<uint8_t, uint8_t, int32_t> m_paramChangedListener;

		float m_masterVolume = 12.0f;

		// JP-8080 EXT IN level knobs, INST/LOWER and VOCAL/UPPER, 64 is unity gain
		std::array<int32_t, 2> m_inputLevel{64, 64};
		std::vector<std::pair<int32_t, int32_t>> m_input;
	};
}

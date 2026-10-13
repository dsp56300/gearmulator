#pragma once

#include "n2xhardware.h"
#include "n2xstate.h"

#include "dsp56kEmu/esaiclock.h"

#include "synthLib/dspClockDevice.h"

namespace n2x
{
	class Hardware;

	class Device : public synthLib::DspClockDevice<dsp56k::EsxiClock>
	{
	public:
		Device(const synthLib::DeviceCreateParams& _params);

		float getSamplerate() const override;
		bool isValid() const override;
		bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		uint32_t getChannelCountIn() override;
		uint32_t getChannelCountOut() override;

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		void getDspClocks(std::vector<dsp56k::EsxiClock*>& _clocks) const override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

	private:
		Hardware m_hardware;
		State m_state;
		uint8_t m_arpHoldMask = 0xff;	// no valid mask, the first block reports the state
	};
}

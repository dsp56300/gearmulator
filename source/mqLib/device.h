#pragma once

#include "microq.h"
#include "mqstate.h"
#include "mqsysexremotecontrol.h"

#include "wLib/wDevice.h"

namespace dsp56k
{
	class EsaiClock;
}

namespace mqLib
{
	class Device : public wLib::Device
	{
	public:
		Device(const synthLib::DeviceCreateParams& _params);
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

		dsp56k::EsxiClock* getDspEsxiClock() const override;

	private:
		MicroQ						m_mq;
		State						m_state;
		SysexRemoteControl			m_sysexRemote;
	};
}

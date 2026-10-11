#pragma once

#include "microq.h"
#include "mqstate.h"

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
		bool getStateWithPendingMidi(std::vector<uint8_t>& _state, synthLib::StateType _type, const std::vector<synthLib::SMidiEvent>& _pendingMidi) override;
		uint32_t getChannelCountIn() override;
		uint32_t getChannelCountOut() override;
		bool canModifyDspClock() const override { return true; }

	protected:
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;

		dsp56k::EsxiClock* getDspEsxiClock() const override;

		void receiveMidiBytes(std::vector<uint8_t>& _bytes) override { m_mq.receiveMidi(_bytes); }
		void sendMidiToSynth(const synthLib::SMidiEvent& _ev) override { m_mq.sendMidiEvent(_ev); }
		bool stateReceive(wLib::Responses& _responses, const synthLib::SMidiEvent& _ev, const wLib::State::Origin _origin) override { return m_state.receive(_responses, _ev, _origin); }

	private:
		void sendLcd(std::vector<synthLib::SMidiEvent>& _dst);
		void sendLcdCgRam(std::vector<synthLib::SMidiEvent>& _dst);
		void sendLeds(std::vector<synthLib::SMidiEvent>& _dst);

		MicroQ						m_mq;
		State						m_state;
	};
}

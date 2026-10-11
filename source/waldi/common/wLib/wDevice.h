#pragma once

#include "wState.h"

#include "dsp56kEmu/esaiclock.h"

#include "synthLib/dspClockDevice.h"

namespace wLib
{
	class SysexRemoteControl;

	class Device : public synthLib::DspClockDevice<dsp56k::EsxiClock>
	{
	public:
		// _sysexRemote may be a member of the product that is constructed after this, it is not used before process()
		Device(const synthLib::DeviceCreateParams& _params, SysexRemoteControl& _sysexRemote);

	protected:
		virtual dsp56k::EsxiClock* getDspEsxiClock() const = 0;
		void getDspClocks(std::vector<dsp56k::EsxiClock*>& _clocks) const override;

		// the product's synth: the MIDI bytes its firmware sent, MIDI for its firmware, and its state mirror
		virtual void receiveMidiBytes(std::vector<uint8_t>& _bytes) = 0;
		virtual void sendMidiToSynth(const synthLib::SMidiEvent& _ev) = 0;
		virtual bool stateReceive(Responses& _responses, const synthLib::SMidiEvent& _ev, State::Origin _origin) = 0;

		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

		std::vector<synthLib::SMidiEvent>	m_customSysexOut;

	private:
		SysexRemoteControl&					m_sysexRemote;
	};
}

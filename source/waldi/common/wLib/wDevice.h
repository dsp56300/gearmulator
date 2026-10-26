#pragma once

#include "wState.h"

#include "dsp56kEmu/esaiclock.h"

#include "synthLib/dspClockDevice.h"
#include "synthLib/frontPanelSysex.h"

namespace wLib
{
	class Device : public synthLib::DspClockDevice<dsp56k::EsxiClock>
	{
	public:
		explicit Device(const synthLib::DeviceCreateParams& _params);

	protected:
		virtual dsp56k::EsxiClock* getDspEsxiClock() const = 0;
		void getDspClocks(std::vector<dsp56k::EsxiClock*>& _clocks) const override;

		// the product's synth: the MIDI bytes its firmware sent, MIDI for its firmware, and its state mirror
		virtual void receiveMidiBytes(std::vector<uint8_t>& _bytes) = 0;
		virtual void sendMidiToSynth(const synthLib::SMidiEvent& _ev) = 0;
		virtual bool stateReceive(Responses& _responses, const synthLib::SMidiEvent& _ev, State::Origin _origin) = 0;

		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

		// the firmware answers dump request _request with the dump, a reply to _requester
		void expectDump(synthLib::MidiEventSource _requester, const SysEx& _request);

		// front panel frames from the editor, before the state cache or the firmware see them. The product handles
		// its events, its answers and the frames it sends go to m_customSysexOut, ahead of the firmware's output
		synthLib::FrontPanelTransport		m_frontPanel;
		std::vector<synthLib::SMidiEvent>	m_customSysexOut;
	};
}

#pragma once

#include "88lib/boards/cm32l.h"
#include "88lib/boards/cm32p.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace emu88Lib
{
	// The CM-64: a CM-32L and a CM-32P in one case, sharing a MIDI IN, a volume control and
	// an output pair. Nothing else connects them. Each runs its own firmware and answers on
	// its own channels - the LA half's power-on default is 2 to 10, the PCM half's 11 to 16 -
	// so pairing them is a wiring job: one MIDI stream to both, their audio summed, their
	// MIDI MESSAGE lamps wired to the one on the bezel. The PCM card slot is the CM-32P's.
	class Cm64
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;
		// Both halves clock their DACs from their own crystals at the same rate.
		static_assert(Cm32l::SampleRate == Cm32p::SampleRate);
		static constexpr uint32_t SampleRate = Cm32l::SampleRate;

		Cm64(const Cm32lRomSet& la, const Cm32pRomSet& pcm, const std::vector<uint8_t>& card = {});
		bool isValid() const { return m_la.isValid() && m_pcm.isValid(); }
		bool hasCard() const { return m_pcm.hasCard(); }
		// Bit 0 = MIDI MESSAGE, lit when either half lights its own.
		uint8_t leds() const { return static_cast<uint8_t>(m_la.leds() | m_pcm.leds()); }
		// The two service displays, the LA half's 20x1 and the PCM half's 16x2.
		const Cm32l& la() const { return m_la; }
		const Cm32p& pcm() const { return m_pcm; }
		// The two boards' line outputs, before they meet. They are summed at the PCM board's
		// mixer, after each has been through its own reconstruction filter, so anything
		// modelling that has to see them apart.
		struct Frames
		{
			SampleFrame la;
			SampleFrame pcm;
		};

		void reset();
		Frames renderFrames();
		// The same pair already summed, for callers with nowhere to put two.
		SampleFrame renderSample();
		void setButtons(uint32_t buttons) { m_la.setButtons(buttons); }
		void addMidiEvent(const synthLib::SMidiEvent& event, uint8_t port = 0);
		// Only the LA board's UART reaches the outside world.
		void readMidiOut(std::vector<synthLib::SMidiEvent>& events) { m_la.readMidiOut(events); }
		void transportDiscontinuity(uint32_t generation);

	private:
		Cm32l m_la;
		Cm32p m_pcm;
	};
}

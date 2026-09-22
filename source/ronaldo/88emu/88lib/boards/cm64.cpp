#include "88lib/boards/cm64.h"

namespace emu88Lib
{
	Cm64::Cm64(const LaRomSet& la, const Cm32pRomSet& pcm, const std::vector<uint8_t>& card)
		: m_la(la), m_pcm(pcm, card)
	{
	}

	void Cm64::reset()
	{
		m_la.reset();
		m_pcm.reset();
	}

	// The two halves are handed over unsummed: they meet at the PCM board's mixer, after each
	// has been through its own output stage, and HardwareDevice models that.
	Cm64::Frames Cm64::renderFrames()
	{
		if(!isValid()) return {};
		return {m_la.renderSample(), m_pcm.renderSample()};
	}

	void Cm64::addMidiEvent(const synthLib::SMidiEvent& event, const uint8_t port)
	{
		// The DIN feeds both opto-isolators, so neither half sees the other's channels filtered
		// out - the firmware on each decides what it answers to.
		m_la.addMidiEvent(event, port);
		m_pcm.addMidiEvent(event, port);
	}

	void Cm64::transportDiscontinuity(const uint32_t generation)
	{
		m_la.transportDiscontinuity(generation);
		m_pcm.transportDiscontinuity(generation);
	}
}

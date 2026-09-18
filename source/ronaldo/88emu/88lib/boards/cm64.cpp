#include "88lib/boards/cm64.h"

#include <algorithm>

#include "synthLib/dac.h"

namespace emu88Lib
{
	namespace
	{
		// Both halves drive the shared interface at full scale, so the sum of two loud boards
		// can run past it. The mixer takes each through a 100k of its own into one 100k of
		// feedback, so they are summed at unity; the clamp is only a backstop for the summed
		// form, and neither half comes anywhere near full scale in practice. The split form
		// hands the two over unsummed and clips nothing.
		constexpr int32_t g_fullScale = int32_t{1} << (synthLib::DacInterfaceBits - 1);
	}

	Cm64::Cm64(const Cm32lRomSet& la, const Cm32pRomSet& pcm, const std::vector<uint8_t>& card)
		: m_la(la), m_pcm(pcm, card)
	{
	}

	void Cm64::reset()
	{
		m_la.reset();
		m_pcm.reset();
	}

	Cm64::Frames Cm64::renderFrames()
	{
		if(!isValid()) return {};
		return {m_la.renderSample(), m_pcm.renderSample()};
	}

	Cm64::SampleFrame Cm64::renderSample()
	{
		const auto frames = renderFrames();
		return {std::clamp(frames.la.first + frames.pcm.first, -g_fullScale, g_fullScale - 1),
		        std::clamp(frames.la.second + frames.pcm.second, -g_fullScale, g_fullScale - 1)};
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

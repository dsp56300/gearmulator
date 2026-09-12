#pragma once

#include <functional>
#include <utility>
#include <vector>

#include "synthLib/midiTypes.h"

namespace emu88Lib
{
	// Generates exactly the requested frames on the calling audio thread.
	// Only future MIDI events survive a block; audio is never queued ahead.
	class Sc88Renderer
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;
		using Render = std::function<SampleFrame()>;
		using SendMidi = std::function<void(const synthLib::SMidiEvent&)>;
		using ReadMidi = std::function<void(std::vector<synthLib::SMidiEvent>&)>;
		using BeforeBlock = std::function<void()>;

		Sc88Renderer(Render _render, SendMidi _sendMidi, ReadMidi _readMidi, BeforeBlock _beforeBlock);
		void processSamples(uint32_t _count, float* _left, float* _right, float _scale,
							std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut);

	private:
		using TimedMidi = std::pair<uint64_t, synthLib::SMidiEvent>;
		void handleTransportDiscontinuity(uint32_t _generation);
		void silenceActiveChannels();
		void trackMidiActivity(const synthLib::SMidiEvent& _event);

		Render m_render;
		SendMidi m_sendMidi;
		ReadMidi m_readMidi;
		BeforeBlock m_beforeBlock;
		uint64_t m_processedSampleOffset = 0;
		uint32_t m_transportGeneration = 0;
		uint64_t m_activeChannels = 0;
		std::vector<TimedMidi> m_tempMidiIn;
	};
} // namespace emu88Lib

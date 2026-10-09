#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "romfile.h"

#include "synthLib/arpeggiator.h"

namespace dsp56k
{
	class Memory;
}

namespace virusLib
{
	// Plays the Virus TI's arpeggiator with synthLib::Arpeggiator, so that it follows the host exactly instead of the
	// MIDI clock. The patterns are read from the firmware that runs, the rest comes from the patches.
	class ArpeggiatorTI
	{
	public:
		// Finds the firmware's preset patterns in DSP memory. False if they are not where this firmware keeps them, the
		// firmware has to keep playing its own arpeggiator then
		bool readPatterns(const dsp56k::Memory& _memory);
		bool isValid() const { return !m_patterns.empty(); }

		// _singles: per MIDI channel, the patch whose arpeggiator plays there, see Microcontroller::getArpeggiatorSingles
		// _midi carries the host's transport, see synthLib::Arpeggiator::process
		void process(const std::array<const ROMFile::TPreset*, 16>& _singles, std::vector<synthLib::SMidiEvent>& _midi,
			std::vector<synthLib::SMidiEvent>& _out, uint32_t _sampleCount, float _samplerate);

	private:
		// A pattern step as the firmware keeps it, in arpeggiator steps
		struct Event
		{
			float length;
			float duration;
			uint8_t velocity;
		};

		// The parts of a patch that make its arpeggio
		using Key = std::array<uint8_t, 105>;

		static Key createKey(const ROMFile::TPreset* _single);
		synthLib::ArpSettings createSettings(uint8_t _channel, const Key& _key) const;

		std::vector<std::vector<Event>> m_patterns;		// [0] is the patch's own pattern, filled from the patch
		std::array<synthLib::Arpeggiator, 16> m_arps;	// one per MIDI channel
		std::array<Key, 16> m_keys{};
		std::array<bool, 16> m_keyValid{};
	};
}

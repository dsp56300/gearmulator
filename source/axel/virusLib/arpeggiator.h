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
	// Plays the Virus' arpeggiator with synthLib::Arpeggiator, so that it follows the host exactly instead of the MIDI
	// clock. The patterns are read from the firmware that runs, the rest comes from the patches. The A, B, C and TI
	// firmwares share the code: what one plays, this plays, see virusArpTest
	class Arpeggiator
	{
	public:
		// Finds the firmware's preset patterns in DSP memory. False if they are not where this firmware keeps them, the
		// firmware has to keep playing its own arpeggiator then. The models differ in a few ways, see createSettings()
		bool readPatterns(const dsp56k::Memory& _memory, DeviceModel _model);
		bool isValid() const { return !m_patterns.empty(); }
		size_t getPatternCount() const { return m_patterns.size(); }

		// _singles: per MIDI channel, the patch whose arpeggiator plays there, see Microcontroller::getArpeggiatorSingles
		// _midi carries the host's transport, see synthLib::Arpeggiator::process
		void process(const std::array<const ROMFile::TPreset*, 16>& _singles, std::vector<synthLib::SMidiEvent>& _midi,
			std::vector<synthLib::SMidiEvent>& _out, uint32_t _sampleCount, float _samplerate);

	private:
		// A pattern step as the firmware keeps it: length and the distance to the next step in 8.16 fixed point, in
		// steps of Arp Clock
		struct Event
		{
			uint32_t length;
			uint32_t duration;
			uint8_t velocity;
		};

		static void compile(const std::vector<Event>& _events, uint32_t _clockUnits, uint32_t _noteLength,
			uint32_t _swing, bool _roundToEven, std::vector<synthLib::ArpStep>& _steps);

		// The parts of a patch that make its arpeggio
		using Key = std::array<uint8_t, 105>;

		static Key createKey(const ROMFile::TPreset* _single);
		synthLib::ArpSettings createSettings(uint8_t _channel, const Key& _key) const;

		std::vector<std::vector<Event>> m_patterns;		// [0] stays empty on the TI, the patch has it
		DeviceModel m_model = DeviceModel::Invalid;
		std::array<synthLib::Arpeggiator, 16> m_arps;	// one per MIDI channel
		std::array<Key, 16> m_keys{};
		std::array<bool, 16> m_keyValid{};
	};
}

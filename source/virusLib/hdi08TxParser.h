#pragma once

#include <array>

#include "../synthLib/midiTypes.h"
#include "../dsp56300/source/dsp56kEmu/types.h"

namespace virusLib
{
	class Hdi08TxParser
	{
	public:
		enum class State
		{
			Default,
			Sysex,
			Preset,
		};

		enum class PatternType
		{
			DspBoot,
			BeatIndicatorOn,
			BeatIndicatorOff,

			Count
		};

		Hdi08TxParser()
		{
			m_patternPositions.fill(0);
		}

		bool append(dsp56k::TWord _data);
		const std::vector<synthLib::SMidiEvent>& getMidiData() const { return m_midiData; }
		void clearMidiData() { m_midiData.clear(); }
		void waitForPreset(uint32_t _byteCount);

		bool waitingForPreset() const
		{
			return m_waitForPreset != 0;
		}

	private:
		std::vector<synthLib::SMidiEvent> m_midiData;
		std::vector<uint8_t> m_sysexData;
		std::vector<uint8_t> m_presetData;
		uint32_t m_waitForPreset = 0;
		State m_state = State::Default;

		std::vector<PatternType> m_matchedPatterns;

		std::array<size_t, static_cast<size_t>(PatternType::Count)> m_patternPositions = {};
	};
}

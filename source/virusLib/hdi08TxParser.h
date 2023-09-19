#pragma once

#include "../synthLib/midiTypes.h"
#include "../dsp56300/source/dsp56kEmu/types.h"

#include <array>
#include <cstddef>

namespace virusLib
{
	class Microcontroller;

	class Hdi08TxParser
	{
	public:
		enum class State
		{
			Default,
			Sysex,
			Preset,
			StatusReport
		};

		enum class PatternType
		{
			DspBoot,

			Count
		};

		Hdi08TxParser(Microcontroller& _mc) : m_mc(_mc)
		{
			m_patternPositions.fill(0);
		}

		bool append(dsp56k::TWord _data);
		const std::vector<synthLib::SMidiEvent>& getMidiData() const { return m_midiData; }
		void clearMidiData() { m_midiData.clear(); }
		void waitForPreset(uint32_t _byteCount);

		bool waitingForPreset() const
		{
			return m_remainingPresetBytes != 0;
		}

		bool hasDspBooted() const
		{
			return m_dspHasBooted;
		}

		void getPresetData(std::vector<uint8_t>& _data);

	private:
		Microcontroller& m_mc;

		std::vector<synthLib::SMidiEvent> m_midiData;
		std::vector<uint8_t> m_sysexData;
		std::vector<uint8_t> m_presetData;
		std::vector<dsp56k::TWord> m_dspStatus;

		uint32_t m_remainingPresetBytes = 0;
		uint32_t m_remainingStatusBytes = 0;

		State m_state = State::Default;

		std::vector<PatternType> m_matchedPatterns;
		std::vector<dsp56k::TWord> m_nonPatternWords;

		std::array<size_t, static_cast<size_t>(PatternType::Count)> m_patternPositions = {};

		bool m_dspHasBooted = false;
	};
}

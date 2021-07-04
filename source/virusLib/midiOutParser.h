#pragma once

#include "midiTypes.h"
#include "../dsp56300/source/dsp56kEmu/types.h"

namespace virusLib
{
	class MidiOutParser
	{
	public:
		bool append(dsp56k::TWord _data);
		const std::vector<SMidiEvent>& getMidiData() const { return m_midiData; }
		void clearMidiData() { m_midiData.clear(); }
	private:
		std::vector<SMidiEvent> m_midiData;
		std::vector<uint8_t> m_data;
	};
}

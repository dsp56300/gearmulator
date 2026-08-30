#pragma once

#include "synthLib/midiTypes.h"

namespace virusLib
{
	// Decodes the MIDI container used by Virus TI Control without interpreting Virus SysEx payloads.
	class TIControlStateDecoder
	{
	public:
		static bool decode(std::vector<synthLib::SMidiEvent>& _events, const synthLib::SysexBuffer& _state);
	};
}

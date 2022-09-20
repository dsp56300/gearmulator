#pragma once

#include <cstdint>
#include <vector>

#include "../synthLib/midiTypes.h"

namespace synthLib
{
	class MidiBufferParser
	{
	public:
		void write(const std::vector<uint8_t>& _data);
		void getEvents(std::vector<synthLib::SMidiEvent>& _events);

	private:
		void flushSysex();
		void flushEvent();

		std::vector<synthLib::SMidiEvent> m_midiEvents;
		std::vector<uint8_t> m_sysexBuffer;
		synthLib::SMidiEvent m_pendingEvent;
		uint32_t m_pendingEventLen = 0;
		bool m_sysex = false;
	};
}

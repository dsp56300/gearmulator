#pragma once

#include <cstdint>
#include <vector>

#include "synthLib/midiTypes.h"

namespace synthLib
{
	class MidiBufferParser
	{
	public:
		void write(const std::vector<uint8_t>& _data);
		void write(uint8_t _data);
		void getEvents(std::vector<synthLib::SMidiEvent>& _events);

		static constexpr uint32_t lengthFromStatusByte(const uint8_t _sb)
		{
		    switch (_sb & 0xf0)
		    {
		    case M_NOTEOFF:
		    case M_NOTEON:
		    case M_POLYPRESSURE:
		    case M_CONTROLCHANGE:
		    case M_PITCHBEND:			return 3;
		    case M_PROGRAMCHANGE:
		    case M_AFTERTOUCH:			return 2;
		    default:
				switch(_sb)
				{
			    case M_STARTOFSYSEX:
			    case M_ENDOFSYSEX:		return 0;
			    case M_QUARTERFRAME:
			    case M_SONGSELECT:		return 2;
			    case M_SONGPOSITION:	return 3;
			    default:				return 1;
				}
		    }
		}

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

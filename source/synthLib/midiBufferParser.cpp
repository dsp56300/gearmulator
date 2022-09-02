#include "midiBufferParser.h"

#include "midiTypes.h"

namespace synthLib
{
	void MidiBufferParser::write(const std::vector<uint8_t>& _data)
	{
		if(_data.empty())
			return;

		for (const auto d : _data)
		{
			if(d == synthLib::M_STARTOFSYSEX)
			{
				flushSysex();
				m_sysex = true;
				m_sysexBuffer.push_back(d);
				continue;
			}

			if(m_sysex)
			{
				if(d == synthLib::M_ENDOFSYSEX)
				{
					flushSysex();
					continue;
				}
				if(d < 0x80)
				{
					m_sysexBuffer.push_back(d);
					continue;
				}
				if(d >= 0xf0)
				{
					// system realtime intercepting sysex
					m_midiEvents.emplace_back(d);
					continue;
				}

				flushSysex();	// aborted sysex
			}

			if(m_pendingEventLen == 0)
				m_pendingEvent.a = d;
			else if(m_pendingEventLen == 1)
				m_pendingEvent.b = d;
			else if(m_pendingEventLen == 2)
				m_pendingEvent.c = d;

			++m_pendingEventLen;

			if(m_pendingEventLen == 1)
			{
				if((m_pendingEvent.a & 0xf0) == 0xf0)
					flushEvent();
			}
			else if(m_pendingEventLen == 2)
			{
				if(m_pendingEvent.a == synthLib::M_QUARTERFRAME || (m_pendingEvent.a & 0xf0) == synthLib::M_AFTERTOUCH)
					flushEvent();
			}
			else if(m_pendingEventLen == 3)
			{
				flushEvent();
			}
		}
	}

	void MidiBufferParser::getEvents(std::vector<synthLib::SMidiEvent>& _events)
	{
		std::swap(m_midiEvents, _events);
		m_midiEvents.clear();
	}

	void MidiBufferParser::flushSysex()
	{
		m_sysex = false;

		if(m_sysexBuffer.empty())
			return;

		synthLib::SMidiEvent ev;
		ev.sysex.swap(m_sysexBuffer);

		if(ev.sysex.back() != synthLib::M_ENDOFSYSEX)
			ev.sysex.push_back(synthLib::M_ENDOFSYSEX);

		m_midiEvents.push_back(ev);
		m_sysexBuffer.clear();
	}

	void MidiBufferParser::flushEvent()
	{
		m_midiEvents.push_back(m_pendingEvent);
		m_pendingEventLen = 0;
	}
}
#include "midiBufferParser.h"

#include "midiTypes.h"

namespace synthLib
{
	void MidiBufferParser::write(const std::vector<uint8_t>& _data)
	{
		for (const auto d : _data)
			write(d);
	}

	void MidiBufferParser::write(uint8_t d)
	{
		if(d == synthLib::M_STARTOFSYSEX)
		{
			flushSysex();
			m_sysex = true;
			m_sysexBuffer.push_back(d);
			return;
		}

		if(m_sysex)
		{
			if(d == synthLib::M_ENDOFSYSEX)
			{
				flushSysex();
				return;
			}
			if(d < 0x80)
			{
				m_sysexBuffer.push_back(d);
				return;
			}
			if(d >= 0xf0)
			{
				// system realtime intercepting sysex
				m_midiEvents.emplace_back(MidiEventSource::Plugin, d);
				return;
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

		if(lengthFromStatusByte(m_pendingEvent.a) == m_pendingEventLen)
			flushEvent();
	}

	void MidiBufferParser::getEvents(std::vector<synthLib::SMidiEvent>& _events)
	{
		_events.insert(_events.end(), m_midiEvents.begin(), m_midiEvents.end());
		m_midiEvents.clear();
	}

	void MidiBufferParser::flushSysex()
	{
		m_sysex = false;

		if(m_sysexBuffer.empty())
			return;

		synthLib::SMidiEvent ev(MidiEventSource::Plugin);
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
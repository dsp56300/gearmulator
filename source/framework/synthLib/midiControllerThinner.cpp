#include "midiControllerThinner.h"

namespace synthLib
{
	MidiControllerThinner::MidiControllerThinner(const uint32_t _intervalSamples, SendCallback _send)
		: m_interval(_intervalSamples)
		, m_send(std::move(_send))
	{
		m_held.reserve(m_streams.size());
	}

	void MidiControllerThinner::write(const SMidiEvent& _ev, const uint32_t _now)
	{
		const auto stream = getStream(_ev);

		if(stream < 0)
		{
			const auto status = _ev.a & 0xf0;
			if(_ev.sysex.empty() && status >= M_NOTEOFF && status < 0xf0)
				release(_now, _ev.a & 0x0f);
			m_send(_ev);
			return;
		}

		auto& s = m_streams[static_cast<size_t>(stream)];

		if(s.sent && _now - s.lastSent < m_interval)
		{
			// too soon after the last one: keep the latest value, flush() sends it when its time has come
			if(!s.listed)
				m_held.push_back(static_cast<uint16_t>(stream));
			s.listed = true;
			s.held = true;
			s.source = _ev.source;
			s.status = _ev.a;
			s.data1 = _ev.b;
			s.data2 = _ev.c;
			return;
		}

		s.held = false;	// replaced by this newer value, which goes out now
		s.sent = true;
		s.lastSent = _now;
		m_send(_ev);
	}

	void MidiControllerThinner::flush(const uint32_t _now)
	{
		release(_now, -1);
	}

	int MidiControllerThinner::getStream(const SMidiEvent& _ev)
	{
		if(!_ev.sysex.empty())
			return -1;

		const auto first = static_cast<int>(_ev.a & 0x0f) * static_cast<int>(StreamsPerChannel);

		switch(_ev.a & 0xf0)
		{
		case M_PITCHBEND:
			return first + 128;
		case M_AFTERTOUCH:	// channel pressure
			return first + 129;
		case M_CONTROLCHANGE:
			if(_ev.b == MC_BANKSELECTMSB || _ev.b == MC_DATAENTRYMSB || _ev.b == MC_BANKSELECTLSB ||
				_ev.b == MC_DATAENTRYLSB || (_ev.b >= MC_SUSTAINPEDAL && _ev.b <= MC_HOLDPEDAL2) ||
				(_ev.b >= MC_DATAINCREMENT0 && _ev.b <= MC_RPNMSB) || _ev.b >= MC_ALLSOUNDOFF)
				return -1;
			return first + _ev.b;
		default:
			return -1;
		}
	}

	void MidiControllerThinner::release(const uint32_t _now, const int _channel)
	{
		for(size_t i = 0; i < m_held.size();)
		{
			const auto stream = m_held[i];
			auto& s = m_streams[stream];

			if(s.held)
			{
				const bool due = _channel >= 0
					? stream / StreamsPerChannel == static_cast<uint32_t>(_channel)
					: _now - s.lastSent >= m_interval;
				if(!due)
				{
					++i;
					continue;
				}
				s.held = false;
				s.lastSent = _now;
				m_send(SMidiEvent(s.source, s.status, s.data1, s.data2));
			}

			s.listed = false;
			m_held[i] = m_held.back();
			m_held.pop_back();
		}
	}
}

#include "midiClock.h"

#include "midiTypes.h"

#include <cmath>

#include "plugin.h"

#if 0
#define LOGMC(S)	LOG(S)
#else
#define LOGMC(S)	do{}while(false)
#endif

namespace synthLib
{
	static constexpr double ClockTicksPerQuarter = 24.0;

	void MidiClock::process(const float _bpm, const float _ppqPos, const bool _isPlaying, const size_t _sampleCount)
	{
		if(_bpm < 1.0f)
			return;

		if(_isPlaying && !m_isPlaying)
		{
			const double ppqPos = _ppqPos;
			m_clockTickPos = (ppqPos - std::floor(ppqPos + 1.0)) * ClockTicksPerQuarter;
			LOGMC("Start at ppqPos=" << ppqPos << ", clock tick offset " << m_clockTickPos);
			m_isPlaying = true;
			m_needsStart = true;
		}
		else if(m_isPlaying && !_isPlaying)
		{
			LOGMC("Stop at ppqPos=" << ppqPos);

			m_isPlaying = false;

			SMidiEvent evStop(MidiEventSource::Internal);
			evStop.a = M_STOP;
			m_plugin.insertMidiEvent(evStop);
			m_clockTickPos = 0.0;
		}

		if(!m_isPlaying)
			return;

		const double quartersPerSecond = _bpm / 60.0;
		const double clockTicksPerSecond = ClockTicksPerQuarter * quartersPerSecond;

		const double clocksPerSample = clockTicksPerSecond * m_plugin.getHostSamplerateInv();

		for(uint32_t i=0; i<static_cast<uint32_t>(_sampleCount); ++i)
		{
			m_clockTickPos += clocksPerSample;

			if (m_clockTickPos < 0.0f)
				continue;

			m_clockTickPos -= 1.0;

			LOGMC("insert tick at " << i);

			SMidiEvent evClock(MidiEventSource::Internal);
			evClock.a = M_TIMINGCLOCK;
			evClock.offset = i;

			if(m_needsStart)
			{
				evClock.a = M_START;
				m_plugin.insertMidiEvent(evClock);
				evClock.a = M_TIMINGCLOCK;

				m_needsStart = false;
			}

			m_plugin.insertMidiEvent(evClock);
		}
	}

	void MidiClock::restart()
	{
		m_needsStart = true;
	}
}

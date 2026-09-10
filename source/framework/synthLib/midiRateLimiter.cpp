#include "midiRateLimiter.h"

#include <algorithm>

#include "midiBufferParser.h"
#include "midiTypes.h"

namespace synthLib
{
	MidiRateLimiter::MidiRateLimiter(WriteCallback _writeCallback) : m_writeCallback(std::move(_writeCallback))
	{
	}

	void MidiRateLimiter::setSamplerate(const float _samplerate)
	{
		m_samplerate = _samplerate;

		if (_samplerate > 0)
			m_samplerateInv = 1.0f / _samplerate;
		else
			m_samplerateInv = 0.0f;
	}

	bool MidiRateLimiter::setRateLimit(const float _bytesPerSecond)
	{
		m_bytesPerSecond = _bytesPerSecond;
		return true;
	}

	void MidiRateLimiter::setDefaultRateLimit()
	{
		setRateLimit(3125.0f); // 3125 bytes per second is the default MIDI baud rate (31.25 kbaud)
	}

	void MidiRateLimiter::disableRateLimit()
	{
		m_bytesPerSecond = 0.0f;
	}

	void MidiRateLimiter::write(SMidiEvent&& _event)
	{

		if (isTransportBound(_event) && _event.transportGeneration < m_transportGeneration)
			return;

		if (!_event.sysex.empty())
			m_pendingSysex.emplace_back(std::move(_event));
		else
			m_pendingRealtime.emplace_back(std::move(_event));
	}

	bool MidiRateLimiter::isTransportBound(const SMidiEvent& _event)
	{
		return _event.sysex.empty() &&
			(_event.source == MidiEventSource::Host || _event.source == MidiEventSource::Internal);
	}

	void MidiRateLimiter::transportDiscontinuity(const uint32_t _generation)
	{
		m_transportGeneration = std::max(m_transportGeneration, _generation);
		for (auto it = m_pendingRealtime.begin(); it != m_pendingRealtime.end();)
		{
			if (isTransportBound(*it) && it->transportGeneration < m_transportGeneration)
			{
				it = m_pendingRealtime.erase(it);
			}
			else
			{
				++it;
			}
		}

		uint16_t channelsToSilence = m_activeChannels;
		if (m_currentEvent && isTransportBound(*m_currentEvent) &&
			m_currentEvent->transportGeneration < m_transportGeneration)
		{
			const auto status = m_currentEvent->a;
			if (status >= M_NOTEOFF && status < M_STARTOFSYSEX)
				channelsToSilence |= static_cast<uint16_t>(1u << (status & 0x0f));

			if (m_currentBytesSent == 0)
			{
				m_pendingBytes.clear();
				m_currentEvent.reset();
				m_currentObsolete = false;
			}
			else
			{
				// Finish a partially transmitted MIDI message so the firmware's
				// parser remains byte-aligned, but do not let it update activity.
				m_currentObsolete = true;
			}
		}

		m_runningStatus = 0;
		// A channel stays owed its silence until the All Sound Off is actually on the wire -
		// completeCurrentEvent() clears the bit then. Clearing it here instead left the queue as
		// the only record of the debt, and the purge above deletes that record on the next
		// discontinuity, so two of them inside the drain window hung every ringing note.
		m_activeChannels = channelsToSilence;
		for (int channel = 15; channel >= 0; --channel)
		{
			if ((channelsToSilence & static_cast<uint16_t>(1u << channel)) == 0)
				continue;
			SMidiEvent allSoundOff(MidiEventSource::Internal,
				static_cast<uint8_t>(M_CONTROLCHANGE | channel), MC_ALLSOUNDOFF, 0);
			allSoundOff.transportGeneration = m_transportGeneration;
			m_pendingRealtime.push_front(std::move(allSoundOff));
		}
	}

	void MidiRateLimiter::processSample()
	{
		// This is elapsed wire-idle time, not a delay to be charged to the next
		// SysEx. Age it even when no event is queued so an idle device does not
		// unexpectedly stall the first message sent much later.
		if (m_remainingSysexPause > 0.0f)
			m_remainingSysexPause = std::max(0.0f, m_remainingSysexPause - m_samplerateInv);

		if (m_bytesPerSecond <= 0.0f || m_samplerate <= 0.0f)
		{
			// No rate limit: drain both the byte-level message in flight and the
			// event-level queues introduced for running status/transport control.
			while (true)
			{
				if (m_pendingBytes.empty())
				{
					if (!m_pendingRealtime.empty())
					{
						auto e = std::move(m_pendingRealtime.front());
						m_pendingRealtime.pop_front();
						beginEvent(std::move(e));
					}
					else if (!m_pendingSysex.empty())
					{
						auto e = std::move(m_pendingSysex.front());
						m_pendingSysex.pop_front();
						beginEvent(std::move(e));
					}
					else
					{
						break;
					}
				}

				while (!m_pendingBytes.empty())
					sendByte();
			}
			return;
		}

		if (m_pendingBytes.empty())
		{
			if (!m_pendingRealtime.empty())
			{
				auto e = std::move(m_pendingRealtime.front());
				m_pendingRealtime.pop_front();
				beginEvent(std::move(e));
			}
			else if (!m_pendingSysex.empty())
			{
				auto e = std::move(m_pendingSysex.front());
				m_pendingSysex.pop_front();
				beginEvent(std::move(e));
			}
			else
			{
				return;
			}
		}

		// The pop above may have queued nothing — e.g. a status byte whose
		// lengthFromStatusByte() is 0 (0xf0/0xf7 arriving as a realtime event).
		// Bail before dereferencing an empty deque.
		if (m_pendingBytes.empty())
			return;

		// if the next byte is a sysex start, we might need to pause first
		auto b = m_pendingBytes.front();

		if (b == 0xf0 && m_remainingSysexPause > 0.0f)
			return;

		m_remainingBytes += m_bytesPerSecond * m_samplerateInv;

		while (m_remainingBytes >= 1.0f && !m_pendingBytes.empty())
		{
			sendByte();
			m_remainingBytes -= 1.0f;
		}
	}

	void MidiRateLimiter::beginEvent(SMidiEvent&& _event)
	{
		m_currentEvent.emplace(std::move(_event));
		m_currentBytesSent = 0;
		m_currentObsolete = false;
		const auto& event = *m_currentEvent;

		if (!event.sysex.empty())
		{
			m_pendingBytes.insert(m_pendingBytes.end(), event.sysex.begin(), event.sysex.end());
			m_currentSysexLength = static_cast<uint32_t>(event.sysex.size());
		}
		else
		{
			const auto len = MidiBufferParser::lengthFromStatusByte(event.a);
			const bool channelMessage = event.a >= M_NOTEOFF && event.a < M_STARTOFSYSEX;
			const bool runningStatus = channelMessage && event.a == m_runningStatus;
			if (len > 0 && !runningStatus) m_pendingBytes.push_back(event.a);
			if (len > 1) m_pendingBytes.push_back(event.b);
			if (len > 2) m_pendingBytes.push_back(event.c);
		}

		if (m_pendingBytes.empty())
			completeCurrentEvent();
	}

	void MidiRateLimiter::setSysexPause(const float _seconds)
	{
		m_sysexPause = _seconds;
	}

	void MidiRateLimiter::setSysexPauseLengthThreshold(uint32_t _size)
	{
		m_sysexPauseLengthThreshold = _size;
	}

	void MidiRateLimiter::sendByte()
	{
		const auto b = m_pendingBytes.front();
		m_pendingBytes.pop_front();
		++m_currentBytesSent;

		if (b >= M_NOTEOFF && b < M_STARTOFSYSEX)
			m_runningStatus = b;
		else if (b >= M_STARTOFSYSEX && b < M_TIMINGCLOCK)
			m_runningStatus = 0;

		if (b == 0xf0)
		{
			m_sendingSysex = true;
			m_currentSysexLength = 1;
		}
		else if (m_sendingSysex)
		{
			// count every byte of the running sysex, or the length-threshold
			// check below can never pass and the pause never engages
			++m_currentSysexLength;

			if (b == 0xf7)
			{
				m_sendingSysex = false;
				if (m_currentSysexLength > m_sysexPauseLengthThreshold)
					m_remainingSysexPause = m_sysexPause;
				m_currentSysexLength = 0;
			}
		}

		m_writeCallback(b);

		if (m_pendingBytes.empty())
			completeCurrentEvent();
	}

	void MidiRateLimiter::completeCurrentEvent()
	{
		if (!m_currentEvent)
			return;

		const auto& event = *m_currentEvent;
		if (!m_currentObsolete && event.sysex.empty())
		{
			const auto command = event.a & 0xf0;
			const auto channel = event.a & 0x0f;
			if ((command == M_NOTEON && event.c != 0) ||
				(command == M_CONTROLCHANGE && event.b == MC_SUSTAINPEDAL && event.c >= 64))
				m_activeChannels |= static_cast<uint16_t>(1u << channel);
			else if (command == M_CONTROLCHANGE && event.b == MC_ALLSOUNDOFF)
				m_activeChannels &= static_cast<uint16_t>(~(1u << channel));
		}

		m_currentEvent.reset();
		m_currentBytesSent = 0;
		m_currentObsolete = false;
	}

}

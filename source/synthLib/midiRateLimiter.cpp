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

	void MidiRateLimiter::write(const SMidiEvent& _event)
	{
		if (!_event.sysex.empty())
		{
			m_pendingBytes.insert(m_pendingBytes.end(), _event.sysex.begin(), _event.sysex.end());
			return;
		}

		const auto len = MidiBufferParser::lengthFromStatusByte(_event.a);

		// we attempt to insert realtime events early, this can only work if there is no sysex message
		// currently being sent and if no other realtime event is at the end of the queue
		if (m_sendingSysex || m_pendingBytes.empty() || m_pendingBytes.back() != 0xf7)
		{
			if (len > 0) m_pendingBytes.push_back(_event.a);
			if (len > 1) m_pendingBytes.push_back(_event.b);
			if (len > 2) m_pendingBytes.push_back(_event.c);
		}
		else
		{
			// insert in front of the next sysex
			for (auto it = m_pendingBytes.begin(); it != m_pendingBytes.end(); ++it)
			{
				if (*it == 0xf0)
				{
					// found first sysex, insert before that
					if (len > 2) it = m_pendingBytes.insert(it, _event.c);
					if (len > 1) it = m_pendingBytes.insert(it, _event.b);
					if (len > 0) it = m_pendingBytes.insert(it, _event.a);

					return;
				}
			}

			// no sysex start found, insert at end

			if (len > 0) m_pendingBytes.push_back(_event.a);
			if (len > 1) m_pendingBytes.push_back(_event.b);
			if (len > 2) m_pendingBytes.push_back(_event.c);
		}
	}

	void MidiRateLimiter::processSample()
	{
		if (m_bytesPerSecond <= 0.0f || m_samplerate <= 0.0f)
		{
			// no rate limit, send all pending bytes
			while (!m_pendingBytes.empty())
				sendByte();
			return;
		}

		if (m_pendingBytes.empty())
			return;

		// if the next byte is a sysex start, we might need to pause first
		auto b = m_pendingBytes.front();

		if (b == 0xf0)
		{
			if (m_remainingSysexPause > 0)
			{
				m_remainingSysexPause -= m_samplerateInv;
				return;
			}
		}

		m_remainingBytes += m_bytesPerSecond * m_samplerateInv;

		while (m_remainingBytes >= 1.0f && !m_pendingBytes.empty())
		{
			sendByte();
			m_remainingBytes -= 1.0f;
		}
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

		if (b == 0xf0)
		{
			m_sendingSysex = true;
			m_currentSysexLength = 1;
		}
		else if (b == 0xf7)
		{
			m_sendingSysex = false;
			if (m_currentSysexLength > m_sysexPauseLengthThreshold)
				m_remainingSysexPause = m_sysexPause;
			m_currentSysexLength = 0;
		}
		else if (m_sendingSysex)
		{
			++m_currentSysexLength;
		}

		m_writeCallback(b);
	}
}

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
		if (!_event.sysex.empty())
			m_pendingSysex.emplace_back(std::move(_event));
		else
			m_pendingRealtime.emplace_back(std::move(_event));
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
		{
			if (!m_pendingRealtime.empty())
			{
				auto e = std::move(m_pendingRealtime.front());
				m_pendingRealtime.pop_front();

				const auto len = MidiBufferParser::lengthFromStatusByte(e.a);

				if (len > 0) m_pendingBytes.push_back(e.a);
				if (len > 1) m_pendingBytes.push_back(e.b);
				if (len > 2) m_pendingBytes.push_back(e.c);
			}
			else if (!m_pendingSysex.empty())
			{
				auto e = std::move(m_pendingSysex.front());
				m_pendingSysex.pop_front();
				m_pendingBytes.insert(m_pendingBytes.end(), e.sysex.begin(), e.sysex.end());

				m_currentSysexLength = static_cast<uint32_t>(e.sysex.size());
			}
			else
			{
				return;
			}
		}

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

		m_writeCallback(b);
	}
}

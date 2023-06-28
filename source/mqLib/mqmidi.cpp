#include "mqmidi.h"

#include <deque>

#include "../mc68k/qsm.h"

namespace mqLib
{
	static constexpr float g_sysexSendDelaySeconds = 0.2f;
	static constexpr uint32_t g_sysexSendDelaySamples = static_cast<uint32_t>(44100.0f * g_sysexSendDelaySeconds);

	Midi::Midi(mc68k::Qsm& _qsm) : m_qsm(_qsm)
	{
	}

	void Midi::process(const uint32_t _numSamples)
	{
		if(m_remainingSysexDelay)
			m_remainingSysexDelay -= std::min(m_remainingSysexDelay, _numSamples);

		while(m_remainingSysexDelay == 0 && !m_transmittingSysex && !m_pendingSysexBuffers.empty())
		{
			const auto& msg = m_pendingSysexBuffers.front();

			for (const auto b : msg)
				m_qsm.writeSciRX(b);

			if(msg.size() > 0xf)
				m_remainingSysexDelay = g_sysexSendDelaySamples;

			m_pendingSysexBuffers.pop_front();
		}
	}

	void Midi::writeMidi(const uint8_t _byte)
	{
		if(_byte == 0xf0)
		{
			m_receivingSysex = true;
		}

		if(m_receivingSysex)
		{
			m_pendingSysexMessage.push_back(_byte);
		}
		else
		{
			m_qsm.writeSciRX(_byte);
		}

		if (_byte == 0xf7)
		{
			m_receivingSysex = false;

			if (!m_pendingSysexMessage.empty())
				m_pendingSysexBuffers.push_back(std::move(m_pendingSysexMessage));

			m_pendingSysexMessage.clear();
		}
	}

	void Midi::readTransmitBuffer(std::vector<uint8_t>& _result)
	{
		std::deque<uint16_t> midiData;
		m_qsm.readSciTX(midiData);
		if (midiData.empty())
			return;

		_result.clear();
		_result.reserve(midiData.size());

		for (const auto data : midiData)
		{
			const uint8_t d = data & 0xff;

			if(d == 0xf0)
				m_transmittingSysex = true;
			else if(d == 0xf7)
				m_transmittingSysex = false;

			_result.push_back(d);
		}
	}
}

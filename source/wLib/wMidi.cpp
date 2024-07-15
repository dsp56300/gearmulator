#include "wMidi.h"

#include <deque>

#include "mc68k/qsm.h"

namespace wLib
{
	// pause 0.1 seconds for a sysex size of 500, delay is calculated for other sysex sizes accordingly
	static constexpr float g_sysexSendDelaySeconds = 0.1f;
	static constexpr uint32_t g_sysexSendDelaySize = 500;

	Midi::Midi(mc68k::Qsm& _qsm) : m_qsm(_qsm)
	{
	}

	void Midi::process(const uint32_t _numSamples)
	{
		std::unique_lock lock(m_mutex);

		if(m_readingSysex)
			return;

		auto remainingSamples = _numSamples;

		while(!m_pendingSysexBuffers.empty())
		{
			if(m_remainingSysexDelay > 0)
			{
				const auto sub = std::min(m_remainingSysexDelay, remainingSamples);
				remainingSamples -= sub;

				m_remainingSysexDelay -= sub;
			}

			if(m_remainingSysexDelay)
				break;

			const auto& msg = m_pendingSysexBuffers.front();

			for (const auto b : msg)
				m_qsm.writeSciRX(b);

			m_remainingSysexDelay = static_cast<uint32_t>(static_cast<float>(msg.size()) * 44100.0f * g_sysexSendDelaySeconds / static_cast<float>(g_sysexSendDelaySize));

			m_pendingSysexBuffers.pop_front();
		}
	}

	void Midi::writeMidi(const uint8_t _byte)
	{
		std::unique_lock lock(m_mutex);

		if(_byte == 0xf0)
		{
			m_writingSysex = true;
		}

		if(m_writingSysex)
		{
			m_pendingSysexMessage.push_back(_byte);
		}
		else
		{
			m_qsm.writeSciRX(_byte);
		}

		if (_byte == 0xf7)
		{
			m_writingSysex = false;

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
				m_readingSysex = true;
			else if(d == 0xf7)
				m_readingSysex = false;

			_result.push_back(d);
		}
	}
}

#include "sciMidi.h"

#include <deque>

#include "mc68k/qsm.h"

#include "synthLib/midiBufferParser.h"

namespace hwLib
{
	// pause 0.1 seconds for a sysex size of 500, delay is calculated for other sysex sizes accordingly
	static constexpr float g_sysexSendDelaySeconds = 0.1f;
	static constexpr uint32_t g_sysexSendDelaySize = 500;

	SciMidi::SciMidi(mc68k::Qsm& _qsm, const float _samplerate) : m_qsm(_qsm), m_samplerate(_samplerate), m_sysexDelaySeconds(g_sysexSendDelaySeconds), m_sysexDelaySize(g_sysexSendDelaySize)
	{
	}

	void SciMidi::process(const uint32_t _numSamples)
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

			m_remainingSysexDelay = static_cast<uint32_t>(static_cast<float>(msg.size()) * m_samplerate * m_sysexDelaySeconds / static_cast<float>(m_sysexDelaySize));

			m_pendingSysexBuffers.pop_front();
		}
	}

	void SciMidi::write(const uint8_t _byte)
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

	void SciMidi::write(const synthLib::SMidiEvent& _e)
	{
		if(!_e.sysex.empty())
		{
			write(_e.sysex);
		}
		else
		{
			write(_e.a);
			const auto len = synthLib::MidiBufferParser::lengthFromStatusByte(_e.a);
			if (len > 1)
				write(_e.b);
			if (len > 2)
				write(_e.c);
		}
	}

	void SciMidi::read(std::vector<uint8_t>& _result)
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

	void SciMidi::setSysexDelay(const float _seconds, const uint32_t _size)
	{
		m_sysexDelaySeconds = _seconds;
		m_sysexDelaySize = _size;
	}
}

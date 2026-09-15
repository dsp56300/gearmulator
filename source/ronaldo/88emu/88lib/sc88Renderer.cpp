#include "sc88Renderer.h"

#include <algorithm>
#include <cstddef>

namespace emu88Lib
{
	Sc88Renderer::Sc88Renderer(Render _render, SendMidi _sendMidi, ReadMidi _readMidi, BeforeBlock _beforeBlock) :
		m_render(std::move(_render)), m_sendMidi(std::move(_sendMidi)), m_readMidi(std::move(_readMidi)),
		m_beforeBlock(std::move(_beforeBlock))
	{
		m_tempMidiIn.reserve(1024);
	}

	void Sc88Renderer::processSamples(uint32_t _count, float* _left, float* _right, float _scale,
									  std::vector<synthLib::SMidiEvent>& _midiIn,
									  std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		for (auto& event : _midiIn)
			m_tempMidiIn.emplace_back(m_processedSampleOffset + event.offset, std::move(event));
		_midiIn.clear();
		if (m_beforeBlock)
			m_beforeBlock();
		for (uint32_t i = 0; i < _count; ++i)
		{
			for (size_t eventIndex = 0; eventIndex < m_tempMidiIn.size();)
			{
				if (m_tempMidiIn[eventIndex].first <= m_processedSampleOffset)
				{
					auto event = std::move(m_tempMidiIn[eventIndex].second);
					m_tempMidiIn.erase(m_tempMidiIn.begin() + static_cast<std::ptrdiff_t>(eventIndex));
					if (event.type == synthLib::MidiEventType::TransportDiscontinuity)
					{
						handleTransportDiscontinuity(event.transportGeneration);
						// Removing stale events can shift earlier entries behind the scan cursor.
						eventIndex = 0;
						continue;
					}
					if (synthLib::isTransportBound(event) && event.transportGeneration < m_transportGeneration)
						continue;
					if (m_sendMidi)
						m_sendMidi(event);
					trackMidiActivity(event);
				}
				else
					++eventIndex;
			}
			const auto sample = m_render();
			if (_left)
				_left[i] = static_cast<float>(sample.first) * _scale;
			if (_right)
				_right[i] = static_cast<float>(sample.second) * _scale;
			++m_processedSampleOffset;
			const auto firstOutput = _midiOut.size();
			if (m_readMidi)
				m_readMidi(_midiOut);
			for (auto j = firstOutput; j < _midiOut.size(); ++j)
				_midiOut[j].offset = i;
		}
	}

	void Sc88Renderer::handleTransportDiscontinuity(const uint32_t _generation)
	{
		m_transportGeneration = std::max(m_transportGeneration, _generation);
		for (auto it = m_tempMidiIn.begin(); it != m_tempMidiIn.end();)
		{
			if (synthLib::isTransportBound(it->second) && it->second.transportGeneration < m_transportGeneration)
				it = m_tempMidiIn.erase(it);
			else
				++it;
		}
		silenceActiveChannels();
	}

	void Sc88Renderer::silenceActiveChannels()
	{
		for (int port = 3; port >= 0; --port)
		{
			for (int channel = 15; channel >= 0; --channel)
			{
				const auto channelBit = static_cast<uint8_t>(port * 16 + channel);
				if ((m_activeChannels & (uint64_t{1} << channelBit)) == 0)
					continue;
				synthLib::SMidiEvent event(synthLib::MidiEventSource::Internal,
										   static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | channel),
										   synthLib::MC_ALLSOUNDOFF, 0);
				event.port = static_cast<uint8_t>(port);
				event.transportGeneration = m_transportGeneration;
				if (m_sendMidi)
					(void)m_sendMidi(event);
			}
		}
		m_activeChannels = 0;
	}

	void Sc88Renderer::trackMidiActivity(const synthLib::SMidiEvent& _event)
	{
		if (!synthLib::isTransportBound(_event) || _event.a < 0x80 || _event.a >= 0xf0)
			return;
		const auto status = _event.a & 0xf0;
		if (_event.port >= 4)
			return;
		const auto channelBit = static_cast<uint8_t>(_event.port * 16 + (_event.a & 0x0f));
		const auto channelMask = uint64_t{1} << channelBit;
		if (status == synthLib::M_NOTEON && _event.c != 0)
			m_activeChannels |= channelMask;
		else if (status == synthLib::M_CONTROLCHANGE && _event.b == synthLib::MC_ALLSOUNDOFF)
			m_activeChannels &= ~channelMask;
	}
} // namespace emu88Lib

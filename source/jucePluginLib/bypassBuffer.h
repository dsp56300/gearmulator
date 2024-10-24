#pragma once

#include <vector>

#include "dsp56kEmu/ringbuffer.h"

namespace pluginLib
{
	class BypassBuffer
	{
	public:
		using ChannelBuffer = dsp56k::RingBuffer<float, 32768, false, true>;

		void write(const float* _data, uint32_t _channel, uint32_t _samples, uint32_t _latency)
		{
			if(_channel >= m_channels.size())
			{
				m_channels.resize(_channel + 1);
				m_latency.resize(_channel + 1);
			}

			auto& ch = m_channels[_channel];
			auto& latency = m_latency[_channel];

			while(_latency > latency)
			{
				++latency;
				ch.push_back(0.0f);
			}

			while(_latency < latency)
			{
				--latency;
				ch.pop_front();
			}

			for(uint32_t i=0; i<_samples; ++i)
				ch.push_back(_data[i]);
		}

		void read(float* _output, uint32_t _channel, const uint32_t _samples)
		{
			if(_channel >= m_channels.size())
				return;

			for(uint32_t i=0; i<_samples; ++i)
				_output[i] = m_channels[_channel].pop_front();
		}

	private:
		std::vector<ChannelBuffer> m_channels;
		std::vector<uint32_t> m_latency;
	};
}

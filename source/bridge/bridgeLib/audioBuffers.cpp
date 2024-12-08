#include "audioBuffers.h"

namespace bridgeLib
{
	AudioBuffers::AudioBuffers() = default;

	void AudioBuffers::writeInput(const synthLib::TAudioInputs& _inputs, const uint32_t _size)
	{
		m_inputSize += _size;

		for(size_t c=0; c<_inputs.size(); ++c)
		{
			auto& in = _inputs[c];

			if(!in)
				continue;

			for(uint32_t i=0; i<_size; ++i)
				m_inputBuffers[c].push_back(in[i]);
		}
	}

	void AudioBuffers::readInput(const uint32_t _channel, std::vector<float>& _data, const uint32_t _numSamples)
	{
		for(uint32_t i=0; i<_numSamples; ++i)
			_data[i] = m_inputBuffers[_channel].pop_front();
	}

	void AudioBuffers::readOutput(const synthLib::TAudioOutputs& _outputs, const uint32_t _size)
	{
		assert(m_outputSize >= _size);
		m_outputSize -= _size;

		for(size_t c=0; c<_outputs.size(); ++c)
		{
			auto* out = _outputs[c];

			if(!out)
				continue;

			for(uint32_t i=0; i<_size; ++i)
				out[i] = m_outputBuffers[c].pop_front();
		}
	}

	void AudioBuffers::writeOutput(const uint32_t _channel, const std::vector<float>& _data, const uint32_t _numSamples)
	{
		for(uint32_t i=0; i<_numSamples; ++i)
			m_outputBuffers[_channel].push_back(_data[i]);
	}

	void AudioBuffers::setLatency(const uint32_t _newLatency, const uint32_t _numSamplesToKeep)
	{
		while(_newLatency > m_latency)
		{
			for (auto& in : m_inputBuffers)
				in.push_back(0.0f);
			++m_latency;
			++m_inputSize;
		}

		while(_newLatency < m_latency && m_inputBuffers.front().size() > _numSamplesToKeep)
		{
			for (auto& in : m_inputBuffers)
				in.pop_front();
			--m_latency;
			--m_inputSize;
		}
	}
}

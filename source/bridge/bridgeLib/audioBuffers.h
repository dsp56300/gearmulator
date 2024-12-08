#pragma once

#include "baseLib/binarystream.h"
#include "dsp56kEmu/ringbuffer.h"

#include "synthLib/audioTypes.h"

namespace bridgeLib
{
	class AudioBuffers
	{
	public:
		static constexpr uint32_t BufferSize = 16384;

		using RingBufferIn = dsp56k::RingBuffer<float, BufferSize, false, true>;
		using RingBufferOut = dsp56k::RingBuffer<float, BufferSize, false, true>;

		AudioBuffers();

		uint32_t getInputSize() const { return m_inputSize; }
		uint32_t getOutputSize() const { return m_outputSize; }
		auto getLatency() const { return m_latency; }

		void onInputRead(const uint32_t _size)
		{
			assert(m_inputSize >= _size);
			m_inputSize -= _size;
		}

		void onOutputWritten(const uint32_t _size)
		{
			m_outputSize += _size;
		}

		void writeInput(const synthLib::TAudioInputs& _inputs, uint32_t _size);
		void readInput(uint32_t _channel, std::vector<float>& _data, uint32_t _numSamples);

		void readOutput(const synthLib::TAudioOutputs& _outputs, uint32_t _size);
		void writeOutput(uint32_t _channel, const std::vector<float>& _data, uint32_t _numSamples);
		void setLatency(uint32_t _newLatency, uint32_t _numSamplesToKeep);

	private:
		std::array<RingBufferIn, std::tuple_size_v<synthLib::TAudioInputs>> m_inputBuffers;
		std::array<RingBufferOut, std::tuple_size_v<synthLib::TAudioOutputs>> m_outputBuffers;

		uint32_t m_inputSize = 0;
		uint32_t m_outputSize = 0;
		uint32_t m_latency = 0;
	};
}

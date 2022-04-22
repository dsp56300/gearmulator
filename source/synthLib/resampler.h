#pragma once

#include <cassert>

#include <functional>
#include <vector>

#include <cstdint>

#include "audiobuffer.h"

namespace synthLib
{
	class Resampler
	{
	public:
		using TProcessFunc = std::function<void(TAudioOutputs&, uint32_t)>;

		Resampler(float _samplerateIn, float _samplerateOut);
		Resampler(const Resampler&) = delete;
		~Resampler();

		uint32_t process(AudioBuffer& _output, size_t _outputOffset, uint32_t _numChannels, uint32_t _numSamples, bool _allowLessOutput, const TProcessFunc& _processFunc)
		{
			TAudioOutputs buffers;
			_output.fillPointers(buffers, _outputOffset);
			return process(buffers, _numChannels, _numSamples, _allowLessOutput, _processFunc);
		}

		uint32_t process(TAudioOutputs& _output, uint32_t _numChannels, uint32_t _numSamples, bool _allowLessOutput, const TProcessFunc& _processFunc);

		float getSamplerateIn() const { return m_samplerateIn; }
		float getSamplerateOut() const { return m_samplerateOut; }

	private:
		uint32_t processResample(TAudioOutputs& _output, uint32_t _numChannels, uint32_t _numSamples, const TProcessFunc& _processFunc);
		void destroyResamplers();
		void setChannelCount(uint32_t _numChannels);

		const float m_samplerateIn;
		const float m_samplerateOut;
		const float m_factorInToOut;
		const float m_factorOutToIn;

		std::vector<void*> m_resamplerOut;

		std::vector< std::vector<float> > m_tempOutput;
		TAudioOutputs m_outputPtrs;
	};
}

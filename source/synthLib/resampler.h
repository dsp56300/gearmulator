#pragma once

#include <cassert>

#include <functional>
#include <vector>
#include <deque>
#include <memory>

#include <cstdint>

#include "audiobuffer.h"
#include "mameResamplers.h"

namespace synthLib
{
	class Resampler
	{
	public:
		enum class Mode : uint8_t
		{
			Legacy,
			MameHq,
			MameLofi
		};

		using TProcessFunc = std::function<void(TAudioOutputs&, uint32_t)>;

		Resampler(float _samplerateIn, float _samplerateOut, Mode _mode = Mode::Legacy);
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
		Mode getMode() const { return m_mode; }

	private:
		uint32_t processResample(const TAudioOutputs& _output, uint32_t _numChannels, uint32_t _numSamples, const TProcessFunc& _processFunc);
		uint32_t processResampleMame(const TAudioOutputs& _output, uint32_t _numChannels, uint32_t _numSamples, const TProcessFunc& _processFunc);
		void ensureMameInput(uint32_t _numChannels, uint32_t _requiredInputSamples, const TProcessFunc& _processFunc);
		void trimMameHistory(uint32_t _numChannels);
		void destroyResamplers();
		void setChannelCount(uint32_t _numChannels);
		bool useMameResampler() const { return m_mode != Mode::Legacy; }

		const float m_samplerateIn;
		const float m_samplerateOut;
		const Mode m_mode;
		const double m_factorInToOut;
		const double m_factorOutToIn;

		double m_inputLen = 0.0;

		std::vector<void*> m_resamplerOut;
		std::vector<std::unique_ptr<MameResampler>> m_mameResamplerOut;
		std::vector<std::deque<float>> m_mameTempOutput;
		int64_t m_mameSourceBaseSample = 0;
		uint64_t m_mameDestSample = 0;

		std::vector< std::vector<float> > m_tempOutput;
		TAudioOutputs m_outputPtrs;
	};
}

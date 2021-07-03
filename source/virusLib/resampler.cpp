#include "resampler.h"

#include <cassert>
#include <utility>

#include "../libresample/include/libresample.h"

#include "../dsp56300/source/dsp56kEmu/fastmath.h"

virusLib::Resampler::Resampler(const float _samplerateIn, const float _samplerateOut)
	: m_samplerateIn(_samplerateIn)
	, m_samplerateOut(_samplerateOut)
	, m_factorInToOut(_samplerateIn / _samplerateOut)
	, m_factorOutToIn(_samplerateOut / _samplerateIn)
{
}

virusLib::Resampler::~Resampler()
{
	destroyResamplers();
}

uint32_t virusLib::Resampler::process(float** _output, const uint32_t _numChannels, const uint32_t _numSamples, bool _allowLessOutput, const TProcessFunc& _processFunc)
{
	setChannelCount(_numChannels);

	if (getSamplerateIn() == getSamplerateOut())
	{
		_processFunc(_output, _numSamples);
		return _numSamples;
	}

	uint32_t index = 0;
	uint32_t remaining = _numSamples;

	while (remaining > 0)
	{
		m_outputPtrs.resize(_numChannels);

		for (uint32_t i = 0; i < _numChannels; ++i)
			m_outputPtrs[i] = &_output[i][index];

		const uint32_t outBufferUsed = processResample(&m_outputPtrs[0], _numChannels, remaining, _processFunc);

		index += outBufferUsed;
		remaining -= outBufferUsed;

		if(_allowLessOutput)
			break;
//		if (remaining > 0)
//			LOG("outBufferUsed " << outBufferUsed << " outLen " << _numSamples);
	}

	return index;
}

uint32_t virusLib::Resampler::processResample(float ** _output, const uint32_t _numChannels, const uint32_t _numSamples, const TProcessFunc& _processFunc)
{
	const uint32_t inputLen = dsp56k::round_int(static_cast<float>(_numSamples) * m_factorInToOut);

	const auto availableInputLen = static_cast<uint32_t>(m_tempOutput[0].size());

	if (availableInputLen < inputLen)
	{
		float* tempBuffers[8];
	
		for (uint32_t i = 0; i < _numChannels; ++i)
		{
			m_tempOutput[i].resize(inputLen, 0.0f);
			tempBuffers[i] = &m_tempOutput[i][availableInputLen];
		}

		_processFunc(tempBuffers, inputLen - availableInputLen);
	}

	uint32_t outBufferUsed = 0;
	int inBufferUsed = 0;

	for (uint32_t i = 0; i < _numChannels; ++i)
	{
		float* output = _output[i];

		outBufferUsed = resample_process(m_resamplerOut[i], m_factorOutToIn, &m_tempOutput[i][0], inputLen, 0, &inBufferUsed, output, _numSamples);

		if (static_cast<uint32_t>(inBufferUsed) < inputLen)
		{
//			LOG("inBufferUsed " << inBufferUsed << " inputLen " << inputLen);
			const size_t remaining = inputLen - inBufferUsed;

			m_tempOutput[i].erase(m_tempOutput[i].begin() + remaining, m_tempOutput[i].end());
		}
		else
		{
			m_tempOutput[i].clear();
		}
	}

	return outBufferUsed;
}

void virusLib::Resampler::destroyResamplers()
{
	for (auto& resampler : m_resamplerOut)
		resample_close(resampler);
	m_resamplerOut.clear();
}

void virusLib::Resampler::setChannelCount(uint32_t _numChannels)
{
	if (m_tempOutput.size() == _numChannels)
		return;

	destroyResamplers();

	m_resamplerOut.resize(_numChannels);
	m_tempOutput.resize(_numChannels);

	for (auto& buf : m_tempOutput)
		buf.clear();

	const auto factor = static_cast<double>(m_factorOutToIn);

	for (auto& resampler : m_resamplerOut)
		resampler = resample_open(1, factor, factor);
}

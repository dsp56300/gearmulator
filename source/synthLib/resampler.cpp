#include "resampler.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "libresample/include/libresample.h"

#include "dsp56kEmu/fastmath.h"

synthLib::Resampler::Resampler(const float _samplerateIn, const float _samplerateOut, const Mode _mode)
	: m_samplerateIn(_samplerateIn)
	, m_samplerateOut(_samplerateOut)
	, m_mode(_mode)
	, m_factorInToOut(_samplerateIn / _samplerateOut)
	, m_factorOutToIn(_samplerateOut / _samplerateIn)
	, m_outputPtrs({})
{
}

synthLib::Resampler::~Resampler()
{
	destroyResamplers();
}

uint32_t synthLib::Resampler::process(TAudioOutputs& _output, const uint32_t _numChannels, const uint32_t _numSamples, bool _allowLessOutput, const TProcessFunc& _processFunc)
{
	assert(_numChannels <= m_outputPtrs.size());

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
		for (uint32_t i = 0; i < _numChannels; ++i)
			m_outputPtrs[i] = &_output[i][index];

		const uint32_t outBufferUsed = useMameResampler()
			? processResampleMame(m_outputPtrs, _numChannels, remaining, _processFunc)
			: processResample(m_outputPtrs, _numChannels, remaining, _processFunc);

		index += outBufferUsed;
		remaining -= outBufferUsed;

		if(_allowLessOutput)
			break;
//		if (remaining > 0)
//			LOG("outBufferUsed " << outBufferUsed << " outLen " << _numSamples);
	}

	return index;
}

uint32_t synthLib::Resampler::processResampleMame(const TAudioOutputs& _output, const uint32_t _numChannels, const uint32_t _numSamples, const TProcessFunc& _processFunc)
{
	if (m_mameResamplerOut.empty())
		return 0;

	const int64_t maxNeeded = m_mameResamplerOut[0]->maxSourceIndexNeeded(m_mameDestSample, _numSamples);
	const int64_t currentEnd = m_mameSourceBaseSample + static_cast<int64_t>(m_mameTempOutput[0].size()) - 1;
	const uint32_t requiredInput = (maxNeeded > currentEnd) ? static_cast<uint32_t>(maxNeeded - currentEnd) : 0u;

	ensureMameInput(_numChannels, requiredInput, _processFunc);

	for (uint32_t i = 0; i < _numChannels; ++i)
	{
		std::fill(_output[i], _output[i] + _numSamples, 0.0f);
		m_mameResamplerOut[i]->apply(m_mameTempOutput[i], m_mameSourceBaseSample, _output[i], m_mameDestSample, _numSamples, 1.0f);
	}

	m_mameDestSample += _numSamples;
	trimMameHistory(_numChannels);
	return _numSamples;
}

uint32_t synthLib::Resampler::processResample(const TAudioOutputs& _output, const uint32_t _numChannels, const uint32_t _numSamples, const TProcessFunc& _processFunc)
{
	// we need to preserve a constant, properly rounded, input length so accumulate the total number of samples we need and subtract the amount we use in this frame
	m_inputLen += static_cast<double>(_numSamples) * m_factorInToOut;
	const uint32_t inputLen = std::max(1, dsp56k::round_int(m_inputLen));
	m_inputLen -= inputLen;

	const auto availableInputLen = static_cast<uint32_t>(m_tempOutput[0].size());

	if (availableInputLen < inputLen)
	{
		TAudioOutputs tempBuffers;
		tempBuffers.fill(nullptr);

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

		outBufferUsed = resample_process(m_resamplerOut[i], m_factorOutToIn, &m_tempOutput[i][0], static_cast<int>(inputLen), 0, &inBufferUsed, output, static_cast<int>(_numSamples));

		if (static_cast<uint32_t>(inBufferUsed) < inputLen)
		{
//			LOG("inBufferUsed " << inBufferUsed << " inputLen " << inputLen);
			const auto remaining = inputLen - inBufferUsed;

			// NOTE: possible bug here, maybe we should only keep the unconsumed tail
			m_tempOutput[i].erase(m_tempOutput[i].begin() + remaining, m_tempOutput[i].end());
		}
		else
		{
			m_tempOutput[i].clear();
		}
	}

	return outBufferUsed;
}

void synthLib::Resampler::ensureMameInput(const uint32_t _numChannels, const uint32_t _requiredInputSamples, const TProcessFunc& _processFunc)
{
	if (_requiredInputSamples == 0)
		return;

	TAudioOutputs tempBuffers;
	tempBuffers.fill(nullptr);
	std::vector<std::vector<float>> temp(_numChannels);
	for (uint32_t i = 0; i < _numChannels; ++i)
	{
		temp[i].resize(_requiredInputSamples, 0.0f);
		tempBuffers[i] = temp[i].data();
	}

	_processFunc(tempBuffers, _requiredInputSamples);

	for (uint32_t i = 0; i < _numChannels; ++i)
	{
		auto& dst = m_mameTempOutput[i];
		dst.insert(dst.end(), temp[i].begin(), temp[i].end());
	}
}

void synthLib::Resampler::trimMameHistory(const uint32_t _numChannels)
{
	if (m_mameResamplerOut.empty() || m_mameTempOutput.empty() || m_mameTempOutput[0].empty())
		return;

	int64_t minNeeded = m_mameResamplerOut[0]->minSourceIndexForOutput(m_mameDestSample);
	uint32_t historyKeep = m_mameResamplerOut[0]->historySize();
	for (uint32_t i = 1; i < _numChannels; ++i)
	{
		minNeeded = std::min(minNeeded, m_mameResamplerOut[i]->minSourceIndexForOutput(m_mameDestSample));
		historyKeep = std::max(historyKeep, m_mameResamplerOut[i]->historySize());
	}

	int64_t safeBase = minNeeded - static_cast<int64_t>(historyKeep);
	if (safeBase < 0)
		safeBase = 0;

	if (safeBase <= m_mameSourceBaseSample)
		return;

	const int64_t drop64 = safeBase - m_mameSourceBaseSample;
	const size_t drop = static_cast<size_t>(std::min<int64_t>(drop64, static_cast<int64_t>(m_mameTempOutput[0].size())));
	if (drop == 0)
		return;

	for (uint32_t i = 0; i < _numChannels; ++i)
		m_mameTempOutput[i].erase(m_mameTempOutput[i].begin(), m_mameTempOutput[i].begin() + static_cast<std::ptrdiff_t>(drop));

	m_mameSourceBaseSample += static_cast<int64_t>(drop);
}

void synthLib::Resampler::destroyResamplers()
{
	for (const auto& resampler : m_resamplerOut)
		resample_close(resampler);
	m_resamplerOut.clear();
	m_mameResamplerOut.clear();
	m_mameTempOutput.clear();
	m_mameSourceBaseSample = 0;
	m_mameDestSample = 0;
}

void synthLib::Resampler::setChannelCount(uint32_t _numChannels)
{
	if (m_tempOutput.size() == _numChannels)
		return;

	destroyResamplers();

	m_resamplerOut.resize(_numChannels);
	m_tempOutput.resize(_numChannels);
	m_mameResamplerOut.resize(_numChannels);
	m_mameTempOutput.resize(_numChannels);

	for (auto& buf : m_tempOutput)
		buf.clear();
	for (auto& buf : m_mameTempOutput)
		buf.clear();

	const auto factor = static_cast<double>(m_factorOutToIn);

	if (useMameResampler())
	{
		const auto mode = (m_mode == Mode::MameLofi) ? MameResamplerMode::Lofi : MameResamplerMode::Hq;
		for (auto& resampler : m_mameResamplerOut)
			resampler = MameResampler::create(mode, static_cast<uint32_t>(m_samplerateIn), static_cast<uint32_t>(m_samplerateOut));
	}
	else
	{
		for (auto& resampler : m_resamplerOut)
			resampler = resample_open(1, factor, factor);
	}
}

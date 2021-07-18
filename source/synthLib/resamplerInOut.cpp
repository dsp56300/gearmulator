#include "resamplerInOut.h"

#include "../dsp56300/source/dsp56kEmu/fastmath.h"
#include "../dsp56300/source/dsp56kEmu/logging.h"

#include <cstring>	// memset/memcpy

using namespace dsp56k;

namespace synthLib
{
	constexpr uint32_t g_channelCount = 2;

	void ResamplerInOut::setDeviceSamplerate(float _samplerate)
	{
		if(m_samplerateDevice == _samplerate)
			return;

		m_samplerateDevice = _samplerate;
		recreate();
	}
	
	void ResamplerInOut::setHostSamplerate(float _samplerate)
	{
		if(m_samplerateHost == _samplerate)
			return;

		m_samplerateHost = _samplerate;
		recreate();
	}

	void ResamplerInOut::recreate()
	{
		if(m_samplerateDevice < 1 || m_samplerateHost < 1)
			return;

		m_out.reset(new Resampler(m_samplerateDevice, m_samplerateHost));
		m_in.reset(new Resampler(m_samplerateHost, m_samplerateDevice));

		m_scaledInputSize = 8;
		m_scaledInput.resize(m_scaledInputSize);
	}

	void ResamplerInOut::scaleMidiEvents(TMidiVec& _dst, const TMidiVec& _src, float _scale)
	{
		_dst.clear();
		_dst.reserve(_src.size());

		for(size_t i=0; i<_src.size(); ++i)
		{
			_dst.push_back(_src[i]);
			_dst[i].offset = floor_int(static_cast<float>(_src[i].offset) * _scale);
		}
	}

	void ResamplerInOut::clampMidiEvents(TMidiVec& _dst, const TMidiVec& _src, uint32_t _offsetMin, uint32_t _offsetMax)
	{
		_dst.clear();
		_dst.reserve(_src.size());

		for(size_t i=0; i<_src.size(); ++i)
		{
			_dst.push_back(_src[i]);
			_dst[i].offset = clamp(_dst[i].offset, static_cast<int>(_offsetMin), static_cast<int>(_offsetMax));
		}
	}

	void ResamplerInOut::extractMidiEvents(TMidiVec& _dst, const TMidiVec& _src, uint32_t _offsetMin, uint32_t _offsetMax)
	{
		_dst.clear();
		_dst.reserve(_src.size());

		for(size_t i=0; i<_src.size(); ++i)
		{
			const auto& m = _src[i];
			if(m.offset < static_cast<int>(_offsetMin) || m.offset > static_cast<int>(_offsetMax))
				continue;;
			_dst.push_back(m);
		}
	}

	void ResamplerInOut::process(float** _inputs, float** _outputs, const TMidiVec& _midiIn, TMidiVec& _midiOut, uint32_t _numSamples, const TProcessFunc& _processFunc)
	{
		if(!m_in || !m_out)
			return;

		const auto devDivHost = m_samplerateDevice / m_samplerateHost;
		const auto hostDivDev = m_samplerateHost / m_samplerateDevice;

		m_scaledInput.ensureSize(static_cast<uint32_t>(static_cast<float>(_numSamples) * devDivHost * 2.0f));

		scaleMidiEvents(m_midiIn, _midiIn, devDivHost);

		m_input.append(_inputs, _numSamples);

		// resample input to buffer scaledInput
		uint32_t scaledSamples;

		// input jitter
		if(m_scaledInputSize > 1)
			scaledSamples = static_cast<uint32_t>(floor_int(static_cast<float>(_numSamples) * devDivHost));
		else
			scaledSamples = static_cast<uint32_t>(ceil_int(static_cast<float>(_numSamples) * devDivHost));

		m_scaledInputSize += m_in->process(m_scaledInput, m_scaledInputSize, g_channelCount, scaledSamples, false, [&](float** _data, uint32_t _numRequestedSamples)
		{
			const auto offset = _numRequestedSamples > m_input.size() ? _numRequestedSamples - m_input.size() : 0;
			if(offset)
			{
				// resampler prewarming, wants more data than we have
				for(size_t c=0; c<g_channelCount; ++c)
				{
					memset(_data[c], 0, sizeof(float) * offset);
					_data[c] += offset;
				}
			}

			const auto count = (_numRequestedSamples - offset);

			if(count)
			{
				for(size_t c=0; c<g_channelCount; ++c)
					memcpy(_data[c], &m_input.getChannel(c)[0], sizeof(float) * count);

				m_input.remove(count);
			}

			m_inputLatency += offset;
		});

		const auto outputSize = m_out->process(_outputs, g_channelCount, _numSamples, false, [&](float** _outs, uint32_t _numProcessedSamples)
		{
			clampMidiEvents(m_processedMidiIn, m_midiIn, 0, _numProcessedSamples-1);
			m_midiIn.clear();

			float* inputs[g_channelCount];
			if(_numProcessedSamples > m_scaledInputSize)
			{
				// resampler prewarming, wants more data than we have
				const auto diff = _numProcessedSamples - m_scaledInputSize;
				m_scaledInput.insertZeroes(diff);
				m_scaledInputSize += diff;
				m_outputLatency += diff;
				LOG("Resampler output latency " << m_outputLatency << " samples");
			}
			m_scaledInput.fillPointers(inputs);
			_processFunc(inputs, _outs, _numProcessedSamples, m_processedMidiIn, m_midiOut);
			m_scaledInput.remove(_numProcessedSamples);
			m_scaledInputSize -= _numProcessedSamples;
		});

		scaleMidiEvents(_midiOut, m_midiOut, hostDivDev);
		m_midiOut.clear();
	}
}

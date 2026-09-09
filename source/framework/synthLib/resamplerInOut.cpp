#include "resamplerInOut.h"

#include <array>
#include <algorithm>

#include "dsp56kBase/fastmath.h"
#include "dsp56kBase/logging.h"

#include <cstring>	// memset/memcpy

using namespace dsp56k;

namespace synthLib
{
	ResamplerInOut::ResamplerInOut(uint32_t _channelCountIn, uint32_t _channelCountOut)
	: m_channelCountIn(_channelCountIn)
	, m_channelCountOut(_channelCountOut)
	, m_scaledInput(_channelCountIn)
	, m_input(_channelCountIn)
	{
	}

	void ResamplerInOut::setResamplerMode(const Resampler::Mode _mode)
	{
		if (m_mode == _mode)
			return;

		m_mode = _mode;
		recreate();
		prepareAlternatives();
	}

	void ResamplerInOut::setDeviceSamplerate(float _samplerate)
	{
		if(m_samplerateDevice == _samplerate)
			return;

		if(m_samplerateDevice > 0)
			for(auto& event : m_midiIn)
				event.offset = floor_int(event.offset * _samplerate / m_samplerateDevice);

		for(auto& alternative : m_alternatives)
		{
			if(alternative->m_samplerateDevice != _samplerate)
				continue;
			// Queued MIDI belongs to the live stream, never to a cached rate.
			alternative->m_midiIn.swap(m_midiIn);
			swapStream(*alternative);
			clearAudioHistory();
			return;
		}

		m_samplerateDevice = _samplerate;
		recreate();
		prepareAlternatives();
	}
	
	void ResamplerInOut::setHostSamplerate(float _samplerate)
	{
		if(m_samplerateHost == _samplerate)
			return;

		m_samplerateHost = _samplerate;
		recreate();
		prepareAlternatives();
	}

	void ResamplerInOut::setSamplerates(const float _hostSamplerate, const float _deviceSamplerate)
	{
		if(m_samplerateDevice == _deviceSamplerate && m_samplerateHost == _hostSamplerate)
			return;

		if(m_samplerateHost == _hostSamplerate)
		{
			setDeviceSamplerate(_deviceSamplerate);
			return;
		}
		if(m_samplerateDevice > 0)
			for(auto& event : m_midiIn)
				event.offset = floor_int(event.offset * _deviceSamplerate / m_samplerateDevice);

		m_samplerateDevice = _deviceSamplerate;
		m_samplerateHost = _hostSamplerate;

		recreate();
		prepareAlternatives();
	}

	void ResamplerInOut::prepareDeviceSamplerates(const std::vector<float>& _samplerates)
	{
		m_dynamicSamplerates = _samplerates;
		prepareAlternatives();
	}

	void ResamplerInOut::prepareAlternatives()
	{
		m_alternatives.clear();
		if(m_samplerateHost < 1)
			return;
		for(const auto rate : m_dynamicSamplerates)
		{
			if(rate < 1 || rate == m_samplerateDevice)
				continue;
			auto alternative = std::make_unique<ResamplerInOut>(m_channelCountIn, m_channelCountOut);
			alternative->m_mode = m_mode;
			alternative->setSamplerates(m_samplerateHost, rate);
			m_alternatives.push_back(std::move(alternative));
		}
	}

	void ResamplerInOut::swapStream(ResamplerInOut& _other)
	{
		using std::swap;
		swap(m_samplerateDevice, _other.m_samplerateDevice);
		swap(m_in, _other.m_in);
		swap(m_out, _other.m_out);
		swap(m_input, _other.m_input);
		swap(m_scaledInput, _other.m_scaledInput);
		swap(m_scaledInputSize, _other.m_scaledInputSize);
		swap(m_midiIn, _other.m_midiIn);
		swap(m_midiOut, _other.m_midiOut);
		swap(m_processedMidiIn, _other.m_processedMidiIn);
		swap(m_scaledMidiIn, _other.m_scaledMidiIn);
		swap(m_inputLatency, _other.m_inputLatency);
		swap(m_outputLatency, _other.m_outputLatency);
	}

	void ResamplerInOut::clearAudioHistory()
	{
		m_in->clearHistory();
		m_out->clearHistory();
		for(uint32_t channel = 0; channel < m_channelCountIn; ++channel)
		{
			if(m_input.size())
				std::fill_n(m_input.getChannel(channel), m_input.size(), 0.0f);
			if(m_scaledInput.size())
				std::fill_n(m_scaledInput.getChannel(channel), m_scaledInput.size(), 0.0f);
		}
	}

	void ResamplerInOut::recreate()
	{
		if(m_samplerateDevice < 1 || m_samplerateHost < 1)
			return;

		m_out.reset(new Resampler(m_samplerateDevice, m_samplerateHost, m_mode));
		m_in.reset(new Resampler(m_samplerateHost, m_samplerateDevice, m_mode));

		m_scaledInputSize = 0;
		m_input.resize(0);
		m_scaledInput.resize(0);
		m_inputLatency = 0;
		m_outputLatency = 0;

		// prewarm to calculate latency
		std::array<std::vector<float>, 12> data;

		TAudioInputs ins;
		TAudioOutputs outs;

		for(size_t i=0; i<data.size(); ++i)
			data[i].resize(512, 0);

		for(size_t i=0; i<ins.size(); ++i)
			ins[i] = i >= data.size() ? nullptr : &data[i][0];

		for(size_t i=0; i<outs.size(); ++i)
			outs[i] = i >= data.size() ? nullptr : &data[i][0];

		TMidiVec midiIn, midiOut;
		midiIn.swap(m_midiIn);
		process(ins, outs, TMidiVec(), midiOut, static_cast<uint32_t>(data[0].size()),
			[&](const TAudioInputs&, const TAudioOutputs& _outs, size_t _count, const TMidiVec&, TMidiVec&)
		{
			for(uint32_t channel = 0; channel < m_channelCountOut; ++channel)
				std::fill_n(_outs[channel], _count, 0.0f);
		});
		midiIn.swap(m_midiIn);
		clearAudioHistory();
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
			_dst[i].offset = clamp(_dst[i].offset, _offsetMin, _offsetMax);
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
				continue;
			_dst.push_back(m);
		}
	}

	void ResamplerInOut::process(const TAudioInputs& _inputs, TAudioOutputs& _outputs, const TMidiVec& _midiIn, TMidiVec& _midiOut, const uint32_t _numSamples, const TProcessFunc& _processFunc)
	{
		if(!m_in || !m_out)
			return;

		if(m_samplerateDevice == m_samplerateHost)
		{
			if(m_midiIn.empty())
			{
				_processFunc(_inputs, _outputs, _numSamples, _midiIn, _midiOut);
				return;
			}
			m_midiIn.insert(m_midiIn.end(), _midiIn.begin(), _midiIn.end());
			_processFunc(_inputs, _outputs, _numSamples, m_midiIn, _midiOut);
			m_midiIn.clear();
			return;
		}

		const auto devDivHost = m_samplerateDevice / m_samplerateHost;
		const auto hostDivDev = m_samplerateHost / m_samplerateDevice;

		m_scaledInput.ensureSize(static_cast<uint32_t>(static_cast<float>(_numSamples) * devDivHost * 2.0f));

		// APPEND this block's (offset-scaled) events to the staged queue.
		// scaleMidiEvents clears its destination, so scaling directly into
		// m_midiIn destroyed any events a previous host block had staged but
		// no device chunk had consumed yet (feedOutput does not necessarily
		// run on every host block) — at mismatched sample rates that silently
		// swallowed incoming MIDI.
		scaleMidiEvents(m_scaledMidiIn, _midiIn, devDivHost);
		m_midiIn.insert(m_midiIn.end(), m_scaledMidiIn.begin(), m_scaledMidiIn.end());

		m_input.append(_inputs, _numSamples);

		auto feedInput = [&](TAudioOutputs& _data, uint32_t _numRequestedSamples)
		{
			const auto offset = _numRequestedSamples > m_input.size() ? _numRequestedSamples - m_input.size() : 0;
			if(offset)
			{
				// resampler prewarming, wants more data than we have
				for(size_t c=0; c<m_channelCountIn; ++c)
				{
					memset(_data[c], 0, sizeof(float) * offset);
					_data[c] += offset;
				}
			}

			const auto count = (_numRequestedSamples - offset);

			if(count)
			{
				for(size_t c=0; c<m_channelCountIn; ++c)
					memcpy(_data[c], &m_input.getChannel(c)[0], sizeof(float) * count);

				m_input.remove(count);
			}

			m_inputLatency += static_cast<uint32_t>(offset);
			if(offset)
			{
				LOG("Resampler input latency " << m_inputLatency << " samples");
			}
		};

		auto feedOutput = [&](const TAudioOutputs& _outs, const uint32_t _numProcessedSamples)
		{
			if(m_channelCountIn)
				m_scaledInputSize += m_in->process(m_scaledInput, m_scaledInputSize, m_channelCountIn, _numProcessedSamples, false, feedInput);

			clampMidiEvents(m_processedMidiIn, m_midiIn, 0, _numProcessedSamples-1);
			m_midiIn.clear();

			TAudioInputs inputs;

			if(m_channelCountIn)
			{
				if(_numProcessedSamples > m_scaledInputSize)
				{
					// resampler prewarming, wants more data than we have
					const auto diff = _numProcessedSamples - m_scaledInputSize;
					m_scaledInput.insertZeroes(diff);
					m_scaledInputSize += diff;
					m_outputLatency += static_cast<uint32_t>(diff);
					LOG("Resampler output latency " << m_outputLatency << " samples");
				}
				m_scaledInput.fillPointers(inputs);
			}
			else
			{
				inputs.fill(nullptr);
			}

			_processFunc(inputs, _outs, _numProcessedSamples, m_processedMidiIn, m_midiOut);

			if(m_channelCountIn)
			{
				m_scaledInput.remove(_numProcessedSamples);
				m_scaledInputSize -= _numProcessedSamples;
			}
		};

		const auto outputSize = m_out->process(_outputs, m_channelCountOut, _numSamples, false, feedOutput);

		scaleMidiEvents(_midiOut, m_midiOut, hostDivDev);
		m_midiOut.clear();
	}
}

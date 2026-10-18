#include "audioLevels.h"

#include <algorithm>
#include <cmath>

#include "juce_audio_basics/juce_audio_basics.h"

namespace pluginLib
{
	void AudioLevels::process(const juce::AudioBuffer<float>& _buffer, const int _numChannels, const int _numSamples, const double _sampleRate)
	{
		const auto channels = static_cast<uint32_t>(std::clamp(std::min(_numChannels, _buffer.getNumChannels()), 0, static_cast<int>(MaxChannels)));
		m_channelCount.store(channels, std::memory_order_relaxed);

		if (!m_enabled.load(std::memory_order_relaxed) || _numSamples <= 0 || _sampleRate <= 0.0)
			return;

		const auto seconds = static_cast<double>(_numSamples) / _sampleRate;
		const auto peakFall = static_cast<float>(std::pow(10.0, -seconds / 1.5));		// -20 dB in 1.5 s
		const auto rmsCoeff = static_cast<float>(1.0 - std::exp(-seconds / 0.3));		// 300 ms

		for (uint32_t ch = 0; ch < channels; ++ch)
		{
			const auto c = static_cast<int>(ch);
			const auto peak = _buffer.getMagnitude(c, 0, _numSamples);
			const auto rms = _buffer.getRMSLevel(c, 0, _numSamples);

			m_peak[ch].store(std::max(peak, m_peak[ch].load(std::memory_order_relaxed) * peakFall), std::memory_order_relaxed);

			const auto ms = m_meanSquare[ch].load(std::memory_order_relaxed);
			m_meanSquare[ch].store(ms + (rms * rms - ms) * rmsCoeff, std::memory_order_relaxed);
		}
	}

	bool AudioLevels::getLevel(const uint32_t _channel, float& _peak, float& _rms)
	{
		m_enabled.store(true, std::memory_order_relaxed);

		if (_channel >= getChannelCount())
			return false;

		_peak = m_peak[_channel].load(std::memory_order_relaxed);
		_rms = std::sqrt(m_meanSquare[_channel].load(std::memory_order_relaxed));
		return true;
	}

	void AudioLevels::stop()
	{
		m_enabled.store(false, std::memory_order_relaxed);

		// the next reader starts from silence, not from where the previous one left off
		for (uint32_t ch = 0; ch < MaxChannels; ++ch)
		{
			m_peak[ch].store(0.0f, std::memory_order_relaxed);
			m_meanSquare[ch].store(0.0f, std::memory_order_relaxed);
		}
	}
}

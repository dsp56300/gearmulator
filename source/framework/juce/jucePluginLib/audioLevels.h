#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace juce
{
	template<typename T> class AudioBuffer;
}

namespace pluginLib
{
	// Peak and RMS level of each output channel, for meters in skins. They are measured on the audio thread, but only
	// while someone reads them: the first read turns the measurement on, stop() turns it off again
	class AudioLevels
	{
	public:
		static constexpr uint32_t MaxChannels = 32;

		// audio thread, with the output of a block
		void process(const juce::AudioBuffer<float>& _buffer, int _numChannels, int _numSamples, double _sampleRate);

		// Any thread. Linear, 1 is full scale. The peak falls by 20 dB in 1.5 seconds, the RMS averages over 300 ms
		bool getLevel(uint32_t _channel, float& _peak, float& _rms);
		uint32_t getChannelCount() const { return m_channelCount.load(std::memory_order_relaxed); }

		void stop();

	private:
		std::array<std::atomic<float>, MaxChannels> m_peak{};
		std::array<std::atomic<float>, MaxChannels> m_meanSquare{};
		std::atomic<uint32_t> m_channelCount{0};
		std::atomic<bool> m_enabled{false};
	};
}

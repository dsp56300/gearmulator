#pragma once

#include <functional>
#include <cstdint>
#include <deque>

namespace synthLib
{
	struct SMidiEvent;

	class MidiRateLimiter
	{
	public:
		using WriteCallback = std::function<void(uint8_t)>;

		MidiRateLimiter(WriteCallback _writeCallback);
		~MidiRateLimiter() = default;

		void setSamplerate(float _samplerate);

		bool setRateLimit(float _bytesPerSecond);
		void setDefaultRateLimit();
		void disableRateLimit();

		void write(const SMidiEvent& _event);

		void processSample();

		// if you want to insert a pause between sysex messages, e.g. to give the synth time to process the data
		void setSysexPause(float _seconds);
		void setSysexPauseLengthThreshold(uint32_t _size);

	private:
		void sendByte();

		WriteCallback m_writeCallback;

		float m_samplerate = 44100.0f;
		float m_samplerateInv = 1.0f / 44100.0f;

		float m_bytesPerSecond = 0.0f;
		float m_remainingBytes = 0.0f;

		std::deque<uint8_t> m_pendingBytes;
		bool m_sendingSysex = false;
		float m_sysexPause = 0.0f;
		float m_remainingSysexPause = 0.0f;
		uint32_t m_sysexPauseLengthThreshold = 0;
		uint32_t m_currentSysexLength = 0;
	};
}

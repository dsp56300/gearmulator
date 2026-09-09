#pragma once

#include <functional>
#include <cstdint>
#include <deque>
#include <optional>

#include "midiTypes.h"

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

		void write(SMidiEvent&& _event);
		void transportDiscontinuity(uint32_t _generation);

		void processSample();

		// if you want to insert a pause between sysex messages, e.g. to give the synth time to process the data
		void setSysexPause(float _seconds);
		void setSysexPauseLengthThreshold(uint32_t _size);

	private:
		void sendByte();
		void beginEvent(SMidiEvent&& _event);
		void completeCurrentEvent();
		static bool isTransportBound(const SMidiEvent& _event);

		WriteCallback m_writeCallback;

		float m_samplerate = 44100.0f;
		float m_samplerateInv = 1.0f / 44100.0f;

		float m_bytesPerSecond = 0.0f;
		float m_remainingBytes = 0.0f;

		std::deque<uint8_t> m_pendingBytes;
		std::deque<SMidiEvent> m_pendingSysex;
		std::deque<SMidiEvent> m_pendingRealtime;
		std::optional<SMidiEvent> m_currentEvent;

		bool m_sendingSysex = false;
		float m_sysexPause = 0.0f;
		float m_remainingSysexPause = 0.0f;
		uint32_t m_sysexPauseLengthThreshold = 0;
		uint32_t m_currentSysexLength = 0;
		uint32_t m_currentBytesSent = 0;
		uint8_t m_runningStatus = 0;
		uint16_t m_activeChannels = 0;
		uint32_t m_transportGeneration = 0;
		bool m_currentObsolete = false;
	};
}

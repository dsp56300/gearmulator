#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "midiTypes.h"

namespace synthLib
{
	// Thins continuous controllers - pitch bend, channel pressure and the CCs that hold a level - to at most one message
	// per channel and controller every given number of samples, the latest value winning. Notes and all other messages
	// pass untouched, and before one of them goes out on a channel, what is held back on that channel goes first, so a
	// note always sees the latest pitch bend or wheel position.
	//
	// A host sends controller data as densely as it likes. A device that takes its MIDI through a UART at the real
	// 31250 baud, into firmware that needs time for every message, copes with only so much of it, and its backlog then
	// delays every note played meanwhile. Thinned to what a hardware controller sends, the notes stay on time. Unlike
	// MidiRateLimiter, which paces bytes and loses nothing, this drops the values that a newer one replaces.
	class MidiControllerThinner
	{
	public:
		using SendCallback = std::function<void(const SMidiEvent&)>;

		MidiControllerThinner(uint32_t _intervalSamples, SendCallback _send);

		// Sends _ev now, or holds it back if it is a controller whose last message went out less than the interval ago.
		// _now is the sample position, compared modulo 2^32, so any running counter will do
		void write(const SMidiEvent& _ev, uint32_t _now);

		// Sends the values held back whose interval has passed. Call it regularly, for example at each event and at the
		// end of each block
		void flush(uint32_t _now);

		// The stream a message is thinned in, one per channel and controller, or -1 if it passes untouched. Bank select,
		// data entry and the NRPN/RPN selects are parts of a sequence, 64-69 are switches and 120-127 channel mode
		// messages: none of those may lose a message
		static int getStream(const SMidiEvent& _ev);

	private:
		// Sends what is held back: everything of _channel, or with _channel < 0 what is due
		void release(uint32_t _now, int _channel);

		static constexpr uint32_t StreamsPerChannel = 130;	// the 128 CCs, pitch bend and channel pressure

		struct Stream
		{
			uint32_t lastSent = 0;
			MidiEventSource source = MidiEventSource::Unknown;
			uint8_t status = 0;
			uint8_t data1 = 0;
			uint8_t data2 = 0;
			bool sent = false;		// has sent a message, so lastSent counts
			bool held = false;
			bool listed = false;	// has an entry in m_held
		};

		const uint32_t m_interval;
		const SendCallback m_send;
		std::array<Stream, 16 * StreamsPerChannel> m_streams{};
		std::vector<uint16_t> m_held;	// at most one entry per stream, capacity reserved up front
	};
}

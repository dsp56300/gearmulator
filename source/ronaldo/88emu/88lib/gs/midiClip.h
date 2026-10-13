#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace emu88Lib::gs
{
	// A short MIDI region: messages laid out one after another with enough room between them for the board, written as
	// a Standard MIDI File that a host turns into a region when it is dropped on a track. The room is a matter of time,
	// not of tempo: events are placed in microseconds and become ticks at 120 bpm, and the file names no tempo, so a
	// host plays it at the project's. One track, for one port: a region plays on one.
	class MidiClip
	{
	public:
		static constexpr uint16_t g_ticksPerQuarter = 480;
		// A channel message takes 1 ms on the wire
		static constexpr uint32_t g_shortSpacing = 1000;
		// An 11-byte DT1 takes 3.5 ms
		static constexpr uint32_t g_sysexSpacing = 5000;
		// GS Reset, GM System On and System Mode Set start the board over, which holds its main loop: DT1s piling up
		// meanwhile can exhaust its buffers
		static constexpr uint32_t g_resetSpacing = 250000;

		struct Event
		{
			uint32_t time;		// microseconds from the start
			std::vector<uint8_t> message;
		};

		// A message, with the room it needs after it
		void add(std::vector<uint8_t> _message);

		bool empty() const { return m_events.empty(); }
		const std::vector<Event>& events() const { return m_events; }

		// Format 1 with its single track: Logic splits a format 0 file into a region per channel, and its SysEx into
		// one of its own. Named after _name, a whole number of bars long.
		std::vector<uint8_t> toSmf(const std::string& _name) const;

	private:
		std::vector<Event> m_events;
		uint32_t m_cursor = 0;
	};

	// Whether a message starts the board over
	bool isResetMessage(const std::vector<uint8_t>& _message);
}

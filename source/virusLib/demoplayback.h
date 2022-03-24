#pragma once

#include <string>
#include <vector>

namespace virusLib
{
	class Microcontroller;

	class DemoPlayback
	{
	public:
		DemoPlayback(Microcontroller& _mc) : m_mc(_mc) {}

		bool loadMidi(const std::string& _filename);
		bool loadBinData(const std::vector<uint8_t>& _data);

		void process(uint32_t _samples);
		
	private:
		enum class EventType
		{
			MidiSysex,
			Midi,
			RawSerial
		};

		struct Event
		{
			EventType type = EventType::MidiSysex;
			std::vector<uint8_t> data;
			uint8_t delay = 0;
		};

		static Event parseSysex(const uint8_t* _data, uint32_t _count);
		Event parseMidi(const uint8_t* _data);

		bool parseData(const std::vector<uint8_t>& _data);

		bool processEvent(const Event& _event) const;

		Microcontroller& m_mc;

		std::vector<Event> m_events;
		int32_t m_remainingDelay = 0;
		uint32_t m_currentEvent = 0;

		uint32_t m_timeScale = 54;
	};
}

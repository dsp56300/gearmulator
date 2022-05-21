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
		virtual ~DemoPlayback() = default;

		virtual bool loadFile(const std::string& _filename);
		virtual bool loadBinData(const std::vector<uint8_t>& _data);

		virtual void process(uint32_t _samples);

	protected:
		void writeRawData(const std::vector<uint8_t>& _data) const;
		void setTimeScale(const float _timeScale) { m_timeScale = _timeScale; }
		void stop();

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

		virtual size_t getEventCount() const						{ return m_events.size(); }
		virtual uint32_t getEventDelay(const size_t _index) const	{ return m_events[_index].delay; }
		virtual bool processEvent(const size_t _index)				{ return processEvent(m_events[_index]); }

	protected:
		Microcontroller& m_mc;

	private:
		std::vector<Event> m_events;
		int32_t m_remainingDelay = 0;
		uint32_t m_currentEvent = 0;

		float m_timeScale = 54.0f;
		bool m_stop = false;
	};
}

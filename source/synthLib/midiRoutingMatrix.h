#pragma once

#include <array>
#include <string_view>

#include "midiTypes.h"

#include "baseLib/binarystream.h"

namespace synthLib
{
	class MidiRoutingMatrix
	{
	public:
		static constexpr uint32_t Size = static_cast<uint32_t>(MidiEventSource::Count);

		enum class EventType : uint8_t
		{
			None          = 0x00,

			Note          = 0x01,
			SysEx         = 0x02,
			Controller    = 0x04,
			PolyPressure  = 0x08,
			Aftertouch    = 0x10,
			PitchBend     = 0x20,
			ProgramChange = 0x40,
			Other         = 0x80,

			All           = 0xff,
		};

		friend constexpr EventType operator | (EventType _a, EventType _b) { return static_cast<EventType>(static_cast<uint8_t>(_a) | static_cast<uint8_t>(_b)); }
		friend constexpr EventType operator & (EventType _a, EventType _b) { return static_cast<EventType>(static_cast<uint8_t>(_a) & static_cast<uint8_t>(_b)); }
		friend constexpr EventType& operator |= (EventType& _a, const EventType _b) { _a = _a | _b; return _a; }
		friend constexpr EventType& operator &= (EventType& _a, const EventType _b) { _a = _a & _b; return _a; }
		friend constexpr EventType operator ~ (EventType& _a) { return static_cast<EventType>(~static_cast<uint8_t>(_a)); }

		void saveChunkData(baseLib::BinaryStream& _binaryStream) const;
		void loadChunkData(baseLib::ChunkReader& _cr);

		MidiRoutingMatrix();

		static EventType getEventType(const SMidiEvent& _event)
		{
			if (!_event.sysex.empty())
				return EventType::SysEx;

			switch (_event.a & 0xf0)
			{
			case M_NOTEON:
			case M_NOTEOFF:			return EventType::Note;
			case M_CONTROLCHANGE:	return EventType::Controller;
			case M_AFTERTOUCH:		return EventType::Aftertouch;
			case M_PITCHBEND:		return EventType::PitchBend;
			case M_POLYPRESSURE:	return EventType::PolyPressure;
			case M_PROGRAMCHANGE:	return EventType::ProgramChange;
			default:				return EventType::Other;
			}
		}

		bool enabled(const MidiEventSource _source, const MidiEventSource _destination, const EventType _type) const
		{
			assert(_source != MidiEventSource::Unknown && _destination != MidiEventSource::Unknown);
			return (get(_source, _destination) & _type) != EventType::None;
		}

		bool enabled(const SMidiEvent& _event, const MidiEventSource _destination) const
		{
			return enabled(_event.source, _destination, getEventType(_event));
		}

		void setEnabled(const MidiEventSource _source, const MidiEventSource _destination, EventType _type, const bool _enabled)
		{
			auto& e = get(_source, _destination);

			if (_enabled)
				e |= _type;
			else
				e &= ~_type;
		}

		static std::string_view toString(MidiEventSource _source);

		const EventType& get(const MidiEventSource _source, const MidiEventSource _destination) const
		{
			return const_cast<MidiRoutingMatrix&>(*this).get(_source, _destination);
		}
	private:
		EventType& get(const MidiEventSource _source, const MidiEventSource _destination)
		{
			return m_matrix[static_cast<uint32_t>(_source)][static_cast<uint32_t>(_destination)];
		}
		std::array<std::array<EventType, Size>, Size> m_matrix;
	};
}

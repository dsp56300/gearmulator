#include "88lib/gs/midiClip.h"

#include "88lib/gs/gs.h"

#include <algorithm>
#include <cmath>

namespace emu88Lib::gs
{
	namespace
	{
		void writeVarLen(std::vector<uint8_t>& _out, uint32_t _value)
		{
			uint8_t bytes[5];
			size_t count = 0;
			bytes[count++] = _value & 0x7f;
			while(_value >>= 7)
				bytes[count++] = static_cast<uint8_t>((_value & 0x7f) | 0x80);
			while(count)
				_out.push_back(bytes[--count]);
		}

		void writeBigEndian(std::vector<uint8_t>& _out, const uint32_t _value, const size_t _bytes)
		{
			for(size_t i = _bytes; i > 0; --i)
				_out.push_back(static_cast<uint8_t>(_value >> ((i - 1) * 8)));
		}

		bool isSysex(const std::vector<uint8_t>& _message)
		{
			return !_message.empty() && _message.front() == 0xf0;
		}

		uint32_t toTicks(const uint32_t _microseconds)
		{
			constexpr double ticksPerMicrosecond = 120.0 * MidiClip::g_ticksPerQuarter / 60000000.0;
			return static_cast<uint32_t>(std::llround(_microseconds * ticksPerMicrosecond));
		}

		void writeMeta(std::vector<uint8_t>& _track, const uint8_t _type, const std::vector<uint8_t>& _data)
		{
			_track.push_back(0x00);	// at tick 0
			_track.push_back(0xff);
			_track.push_back(_type);
			writeVarLen(_track, static_cast<uint32_t>(_data.size()));
			_track.insert(_track.end(), _data.begin(), _data.end());
		}
	}

	bool isResetMessage(const std::vector<uint8_t>& _message)
	{
		// GM System On, GM2 System On
		if(_message.size() == 6 && _message[0] == 0xf0 && _message[1] == 0x7e && _message[3] == 0x09)
			return true;
		Dt1 dt1;
		return parseDt1(_message.data(), _message.size(), dt1) &&
		       ((dt1.address == g_gsReset && dt1.size && dt1.data[0] == 0x00) || dt1.address == g_systemModeSet);
	}

	void MidiClip::add(std::vector<uint8_t> _message)
	{
		if(_message.empty())
			return;
		const auto spacing = isResetMessage(_message) ? g_resetSpacing : isSysex(_message) ? g_sysexSpacing : g_shortSpacing;
		add(std::move(_message), spacing);
	}

	void MidiClip::add(std::vector<uint8_t> _message, const uint32_t _spacing)
	{
		if(_message.empty())
			return;
		m_events.push_back({m_cursor, std::move(_message)});
		m_cursor += _spacing;
	}

	std::vector<uint8_t> MidiClip::toSmf(const std::string& _name) const
	{
		std::vector<uint8_t> track;
		// Hosts name the region after the track
		writeMeta(track, 0x03, std::vector<uint8_t>(_name.begin(), _name.end()));

		uint32_t lastTick = 0;
		for(const auto& e : m_events)
		{
			const auto tick = std::max(toTicks(e.time), lastTick);
			writeVarLen(track, tick - lastTick);
			lastTick = tick;
			if(isSysex(e.message))
			{
				// F0, then the length of the rest including the trailing F7
				track.push_back(0xf0);
				writeVarLen(track, static_cast<uint32_t>(e.message.size() - 1));
				track.insert(track.end(), e.message.begin() + 1, e.message.end());
			}
			else
			{
				track.insert(track.end(), e.message.begin(), e.message.end());
			}
		}

		// End of track on a bar line, so that the region is a whole number of bars
		constexpr uint32_t bar = g_ticksPerQuarter * 4;
		const auto length = toTicks(m_cursor);
		const auto end = std::max({bar, (length + bar - 1) / bar * bar, lastTick});
		writeVarLen(track, end - lastTick);
		track.insert(track.end(), {0xff, 0x2f, 0x00});

		std::vector<uint8_t> smf = {'M', 'T', 'h', 'd'};
		writeBigEndian(smf, 6, 4);
		writeBigEndian(smf, 1, 2);	// format 1, see the header
		writeBigEndian(smf, 1, 2);	// one track
		writeBigEndian(smf, g_ticksPerQuarter, 2);
		smf.insert(smf.end(), {'M', 'T', 'r', 'k'});
		writeBigEndian(smf, static_cast<uint32_t>(track.size()), 4);
		smf.insert(smf.end(), track.begin(), track.end());
		return smf;
	}
}

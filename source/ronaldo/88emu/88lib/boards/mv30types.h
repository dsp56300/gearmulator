#pragma once

#include <array>
#include <cstdint>

namespace emu88Lib
{
	// Front panel switches, numbered as the TC23SC060 reports them: scan row * 8 + column.
	enum class Mv30Button : uint8_t
	{
		PatternEdit = 0, Up = 2, Enter = 3, Ctrl = 4, Num0 = 5, Track1 = 6, Jump = 7,
		TrackEdit = 8, Left = 10, Num3 = 11, Compu = 12, Num2 = 13, Track2 = 14, F1 = 15,
		System = 16, Right = 18, Num6 = 19, Manual = 20, Num5 = 21, Track3 = 22, F2 = 23,
		Disk = 24, Down = 26, Num9 = 27, SongSelect = 28, Num8 = 29, Track4 = 30, F3 = 31,
		PatternMicroscope = 32, PatternRealTime = 33, Insert = 34, Record = 35, Status = 36, Num1 = 37, Track5 = 38, F4 = 39,
		TrackMicroscope = 40, TrackRealTime = 41, Delete = 42, MeasureDec = 43, Locate = 44, Num4 = 45, Track6 = 46, F5 = 47,
		TimbreEdit = 48, CompuMix = 49, StartStop = 51, Mark = 52, Num7 = 53, Track7 = 54, Exit = 55,
		ChainLoad = 56, Play = 57, MeasureInc = 59, Tempo = 60, Shift = 61, Track8 = 62,
	};

	// The MV-30's part settings and timbre names as its operating system keeps them in the current song: what the
	// Play screen shows. The sound section takes no SysEx (owner's manual p. 6-35, and the firmware's MIDI parser
	// never looks into one), so this is read off the board's RAM; of it MIDI reaches the timbre (program change), the
	// level (CC7) and the pan (CC10).
	struct Mv30Parts
	{
		static constexpr uint8_t PartCount = 8;
		static constexpr uint8_t TimbreCount = 128;
		static constexpr uint8_t NameLength = 12;
		// The timbre value of a part that plays the rhythm timbre, which only the front panel selects
		static constexpr uint8_t RhythmTimbre = 0x80;

		struct Part
		{
			uint8_t timbre = 0;			// 0-127 for timbre 1-128, or RhythmTimbre
			uint8_t output = 0;			// 0-4: Dry, Rev, Cho, Dir1, Dir2
			uint8_t level = 127;		// 0-127
			int8_t pan = 0;				// -7 (7L) to 7 (R7)
			uint8_t voiceReserve = 0;
			uint8_t channel = 0xff;		// 0-15, anything above: receives no channel

			bool operator==(const Part& _other) const
			{
				return timbre == _other.timbre && output == _other.output && level == _other.level &&
				       pan == _other.pan && voiceReserve == _other.voiceReserve && channel == _other.channel;
			}
			bool operator!=(const Part& _other) const { return !(*this == _other); }
		};

		// Read from a running OS; false before it has started, when the rest is empty
		bool valid = false;
		std::array<Part, PartCount> parts{};
		// Padded with spaces or zeros, as the OS keeps them
		std::array<std::array<char, NameLength>, TimbreCount> timbreNames{};

		bool operator==(const Mv30Parts& _other) const
		{
			return valid == _other.valid && parts == _other.parts && timbreNames == _other.timbreNames;
		}
		bool operator!=(const Mv30Parts& _other) const { return !(*this == _other); }

		// The CC10 value that sets a pan step: the middle of the range the OS maps to it (0-5 to 7L, 6-14 to 6L,
		// nine values each, 60-69 to the centre, 124-127 to R7)
		static constexpr uint8_t panController(const int8_t _pan)
		{
			constexpr uint8_t values[15] = {2, 10, 19, 28, 37, 46, 55, 64, 74, 83, 92, 101, 110, 119, 126};
			return values[(_pan < -7 ? -7 : _pan > 7 ? 7 : _pan) + 7];
		}
	};
}

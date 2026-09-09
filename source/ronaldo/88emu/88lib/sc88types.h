#pragma once

#include <cstdint>

namespace emu88Lib
{
	// The VL has different panel parameters and uses P6DR for LCD power.
	// Rom::model() identifies the board by its firmware hash.
	enum class Model { Sc88, Sc88VL };

	// =====================================================================
	// Board constants
	// =====================================================================

	// Audio output rate of the XP DAC.
	//
	// The XP is clocked at 24.576 MHz — a standard audio crystal — and the
	// board runs at 32 kHz, i.e. exactly 768 XP clocks per sample. (The
	// NukedSC55 reference opens its SDL device at 64000 Hz for this romset,
	// but that number belongs to its wrong-chip SC-55 PCM reuse, not to the
	// XP. Contrast the SC-55mk2 in sc55mk2.h, where the GP's own clock and
	// voice-slot cadence really do produce an odd 66207 Hz.)
	static constexpr uint32_t g_xpClockHz  = 24576000;
	static constexpr uint32_t g_sampleRate = 32000;

	// H8/510 phi (system/peripheral clock).
	//
	// 10 MHz — the system clock produced from the board's 20 MHz oscillator.
	// It is the whole story now: the CPU core charges every instruction its
	// Appendix-A.4 states, so the same rate paces the instruction stream, the
	// timers, serial I/O and the ADC. The board used to need twice this as a
	// separate execution budget to reach the real unit's roughly six-second
	// boot; with the current core it boots in that time on the part's own
	// clock, so the compensation is gone.
	static constexpr uint64_t g_cpuClockHz = 10000000;

	// =====================================================================
	// Front panel
	// =====================================================================

	// Button matrix. The firmware selects one of four columns by writing a
	// one-hot value to the gate array's scan port (0x0F:0x00FE) and then reads
	// the same address back for the eight rows of that column, active-low.
	// Button index = bit position in the 32-bit bitmap the board keeps:
	// column = index / 8, row = index % 8.
	//
	// Layout taken from the NukedSC55 reference's SC-55 matrix plus its SC-88
	// additions; the SC-88-only entries reuse the SC-55 holes.
	enum class Button : uint8_t
	{
		Power        = 0,
		Eq           = 1,	// SC-88
		InstMap      = 2,	// SC-88
		InstL        = 3,
		InstR        = 4,
		InstMute     = 5,
		InstAll      = 6,
		Preview      = 7,	// SC-88

		MidiChL      = 8,
		MidiChR      = 9,
		ChorusL      = 10,
		ChorusR      = 11,
		PanL         = 12,
		PanR         = 13,
		PartR        = 14,
		// 15 unused

		KeyShiftL    = 16,
		KeyShiftR    = 17,
		ReverbL      = 18,
		ReverbR      = 19,
		LevelL       = 20,
		LevelR       = 21,
		PartL        = 22,
		// 23 unused

		UserInst     = 24,	// SC-88
		Select       = 25,	// SC-88
		VibRateL     = 26,	// SC-88
		VibRateR     = 27,	// SC-88
		VibDepthL    = 28,	// SC-88
		VibDepthR    = 29,	// SC-88
		VibDelayL    = 30,	// SC-88
		VibDelayR    = 31,	// SC-88

		Count        = 32
	};

	static constexpr uint32_t g_buttonCount = static_cast<uint32_t>(Button::Count);

	constexpr uint32_t buttonBit(const Button _b)
	{
		return 1u << static_cast<uint8_t>(_b);
	}
}

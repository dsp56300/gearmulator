#pragma once

// SC-88 front-panel display renderer.
//
// The SC-88's LCD is not a plain character grid: the firmware writes an
// HD44780's DDRAM, but the glass scatters those cells across the panel as one
// 19-character instrument line, six 3-character numeric fields, two 4-segment
// level meters and an L/R indicator. This reproduces that mapping (it is the
// same one the NukedSC55 reference uses for this romset — verified against our
// own boot dump, where DDRAM columns 20..23 hold exactly the level-meter CGRAM
// glyphs 0x00..0x07).
//
#include <cstdint>

extern uint8_t lcd_font[240][10];

namespace sc88panel
{
	constexpr int kWidth  = 741;
	constexpr int kHeight = 268;

	// ---- Panel field placement (row, column) ----
	//
	// Each entry maps a run of DDRAM cells to a start position. DDRAM index =
	// line * 40 + column, matching emu88Lib::Lcd::ddRam().
	struct TextField
	{
		int      ddRamStart;
		int      count;
		int32_t  row;
		int32_t  column;
	};

	constexpr TextField kTextFields[] =
	{
		// Top instrument line: 3-char map/variation + 16-char name.
		{  0,  3,  11,  34},
		{  3, 16,  11, 153},
		// Six 3-character numeric readouts, two per row.
		{ 40,  3,  75,  34}, { 43, 3,  75, 153},
		{ 49,  3, 139,  34}, { 46, 3, 139, 153},
		{ 52,  3, 203,  34}, { 55, 3, 203, 153},
	};

	// The L/R indicator is a lamp, driven by bit 0 of DDRAM cell 58.
	constexpr int kLrDdRam = 58;

	// Level meters: 4 cells per channel, taken from DDRAM 20..23 (line 0) and
	// 60..63 (line 1).
	constexpr int kLevelDdRam[2] = {20, 60};

	// 12x11 "L" and "R" glyphs and their panel positions.
	inline constexpr uint8_t kLrGlyph[2][12][11] =
	{
		{
			{1,1,0,0,0,0,0,0,0,0,0}, {1,1,0,0,0,0,0,0,0,0,0},
			{1,1,0,0,0,0,0,0,0,0,0}, {1,1,0,0,0,0,0,0,0,0,0},
			{1,1,0,0,0,0,0,0,0,0,0}, {1,1,0,0,0,0,0,0,0,0,0},
			{1,1,0,0,0,0,0,0,0,0,0}, {1,1,0,0,0,0,0,0,0,0,0},
			{1,1,0,0,0,0,0,0,0,0,0}, {1,1,0,0,0,0,0,0,0,0,0},
			{1,1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1,1},
		},
		{
			{1,1,1,1,1,1,1,1,1,0,0}, {1,1,1,1,1,1,1,1,1,1,0},
			{1,1,0,0,0,0,0,0,1,1,0}, {1,1,0,0,0,0,0,0,1,1,0},
			{1,1,0,0,0,0,0,0,1,1,0}, {1,1,1,1,1,1,1,1,1,1,0},
			{1,1,1,1,1,1,1,1,1,0,0}, {1,1,0,0,0,0,0,1,1,0,0},
			{1,1,0,0,0,0,0,0,1,1,0}, {1,1,0,0,0,0,0,0,1,1,0},
			{1,1,0,0,0,0,0,0,0,1,1}, {1,1,0,0,0,0,0,0,0,1,1},
		},
	};
	inline constexpr int32_t kLrPos[2][2] = {{70, 264}, {232, 264}};

	// Draw LCD dots over transparency. DDRAM is 80 bytes, CGRAM 64 bytes;
	// colours are packed ARGB and _blank switches all dots off.
	void renderOverlay(uint32_t* _dst, const uint8_t* _ddRam, const uint8_t* _cgRam,
	                   bool _blank, uint32_t _on, uint32_t _off);
}

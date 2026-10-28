#include "88lib/boards/sc880Lcd.h"

namespace emu88Lib
{

	namespace
	{
		// Where each segment of a digit sits in a nibble: {line, bit} for a digit in the high nibble
		// of its byte; a digit in the low nibble uses the bits four lower. Taken from the firmware's
		// digit tables at 11:0EBC and 11:0EE4, and the only wiring that gives the standard shape for
		// all ten digits. The eighth bit, line 1 bit 4, is cleared by every digit write; it is
		// taken as the decimal point.
		struct SegmentBit { uint8_t line; uint8_t bit; };
		constexpr SegmentBit g_segmentBits[8] = {
			{0, 7},	// a
			{0, 4},	// b
			{0, 5},	// c
			{1, 5},	// d
			{1, 6},	// e
			{1, 7},	// f
			{0, 6},	// g
			{1, 4},	// point
		};

		// The 25 digits, left to right, as the byte column and nibble that drives them (shift 0 = high
		// nibble, 4 = low, 0xff = never driven by the firmware). The part display A, 0, 1 is slots 2-4
		// with two dark digits before it. The seven mode "o" digits are the nibbles the firmware lights
		// for the lamps, one under each printed label, three digits apart and ending on the 25th; the
		// slots in between are not known to carry anything. Checked against a photograph with every
		// digit's segment marked.
		struct DigitNibble { uint8_t column; uint8_t shift; };
		constexpr DigitNibble g_digits[Sc880Lcd::StripDigits] = {
			{1, 0}, {1, 4},
			{2, 0}, {2, 4}, {3, 0},		// part: group letter, tens, ones
			{4, 4}, {5, 0}, {5, 4},		// PERF
			{6, 0}, {6, 4}, {7, 0},		// PATCH
			{8, 4},	{9, 0}, {8, 0},		// TONE
			{10, 0}, {10, 4}, {11, 0},	// RHY
			{12, 4}, {13, 0}, {13, 4},	// M-FX
			{14, 0}, {14, 4}, {15, 0},	// SYS
			{15, 4}, {16, 0},			// UTIL
		};
	}
	void Sc880Lcd::reset()
	{
		m_ram.fill(0);
		m_column = 0;
		m_line = 0;
		m_contrast = 0;
		m_initialised = false;
	}

	void Sc880Lcd::writeInstruction(const uint8_t _instr)
	{
		m_initialised = true;
		if(_instr >= 0xe0)
			m_contrast = _instr & 0x1f;
		else if(_instr >= 0x80)
			m_line = _instr & 0x3f;
		else if(_instr >= 0x20 && _instr < 0x40)
			m_column = _instr & 0x1f;
	}

	void Sc880Lcd::writeData(const uint8_t _data)
	{
		m_ram[m_line * Columns + m_column] = _data;
		m_line = (m_line + 1) & (Lines - 1);
	}

	void Sc880Lcd::renderMono(std::vector<uint8_t>& _output, std::array<uint8_t, 25>& _dseg) const
	{
		_output.assign(Width * Height, 0);
		for(uint32_t y = 0; y < Height; ++y)
		{
			for(uint32_t x = 0; x < Width; ++x)
				_output[y * Width + x] = (m_ram[(y + 2) * Columns + x / 8] >> (7 - x % 8)) & 1;
		}
		for(uint32_t i = 0; i < Sc880Lcd::StripDigits; ++i)
		{
			const auto& digit = g_digits[i];
			if(digit.shift == 0xff)
				continue;
			uint8_t segments = 0;
			for(uint32_t s = 0; s < 8; ++s)
			{
				const auto [line, bit] = g_segmentBits[s];
				if(stripBit(line, digit.column, bit - digit.shift))
					segments |= static_cast<uint8_t>(1u << s);
			}
			_dseg[i] = segments;
		}
	}
}

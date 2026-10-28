#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace emu88Lib
{
	// The SC-880's dot-matrix display, as its firmware drives it through the gate array's
	// LCD port (the port the SC-88Pro's character module hangs off). The controller chip
	// is not identified; this models what the firmware relies on, read from its refresh
	// routine (10:3BFC) and its init sequence (10:2F18):
	//
	//   0x20-0x3F  byte column: eight dots across, MSB leftmost
	//   0x80-0xBF  line, counting down from the top; every data byte moves it one line down
	//   0xE0-0xFF  contrast, written as 0xE0 | the system setting (10:2F9B)
	//
	// The panel is 17 byte columns by 34 lines, 136 x 34 dots: the firmware rewrites
	// exactly that area, column by column, each in bursts from lines 0x00, 0x0C, 0x18 and
	// 0x21. The init also sends 0x03, 0xF0, 0x01, 0x05, 0x13, 0x08 and 0x40, which are
	// kept but not interpreted: none of them is sent again, so the display is taken as on
	// once the controller has seen its first instruction.
	class Sc880Lcd
	{
	public:
		static constexpr uint32_t Width = 136;
		static constexpr uint32_t Height = 32;
		static constexpr uint32_t StripDigits = 25;

		void reset();
		void writeInstruction(uint8_t _instr);
		void writeData(uint8_t _data);

		bool isDisplayOn() const { return m_initialised; }
		uint8_t contrast() const { return m_contrast; }

		// One byte per dot, row major, Width x Height: 1 lit, 0 dark.
		void renderMono(std::vector<uint8_t>& _output, std::array<uint8_t, 25>& _dseg) const;

	private:
		bool stripBit(const uint32_t _line, const uint32_t _column, const uint32_t _bit) const
		{
			return (m_ram[_line * Columns + _column] >> _bit) & 1;
		}
		static constexpr uint32_t Columns = 32;		// what a 5-bit column address reaches
		static constexpr uint32_t Lines = 64;		// what a 6-bit line address reaches

		std::array<uint8_t, Columns * Lines> m_ram{};
		uint8_t m_column = 0;
		uint8_t m_line = 0;
		uint8_t m_contrast = 0;
		bool m_initialised = false;
	};
}

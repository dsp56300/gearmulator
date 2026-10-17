#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace hwLib
{
	// Sitronix ST7565 / Epson SED1565 class 65 x 132 LCD driver, as behind the
	// 128 x 64 graphic panels of the early-2000s Roland units.
	//
	// The host sees a command port and a data port.  Display RAM is eight
	// pages of 132 column bytes, one page per eight rows, bit 0 at the top;
	// the column address auto-increments on data writes and stops at the last
	// column.  Commands cover the page / column address, the display start
	// line, segment (ADC) and common output direction, normal / reverse video,
	// all-points-on, display on / off and the two-byte electronic volume and
	// static indicator settings; the power, bias and resistor-ratio commands
	// are accepted and ignored.
	//
	// renderMono() rasterises the visible panel (panelWidth x 64, 0 = off,
	// 1 = on) taking the start line, the two directions and reverse video
	// into account; a display that is switched off renders blank.  A panel
	// whose leftmost column hangs off the highest segment output is
	// constructed with _segmentsReversed, so that the firmware's ADC choice
	// comes out the way it does on the hardware.
	class St7565
	{
	public:
		static constexpr uint32_t Columns = 132;
		static constexpr uint32_t Pages = 8;
		static constexpr uint32_t Rows = 64;

		explicit St7565(uint32_t _panelWidth = 128, bool _segmentsReversed = false);
		void reset();

		void writeCommand(uint8_t _cmd);
		void writeData(uint8_t _value);
		uint8_t readData();

		uint32_t width() const { return m_width; }
		uint32_t height() const { return Rows; }
		bool isDisplayOn() const { return m_displayOn; }
		bool isInverse() const { return m_inverse; }
		uint8_t contrast() const { return m_volume; }
		const std::array<uint8_t, Columns * Pages>& memory() const { return m_ram; }
		// Bumped on every write that can change what the panel shows.
		uint64_t revision() const { return m_revision; }

		// One byte per pixel, row-major, width() x height().
		void renderMono(std::vector<uint8_t>& _out) const;

	private:
		uint32_t m_width;
		bool m_segmentsReversed;
		std::array<uint8_t, Columns * Pages> m_ram{};
		uint8_t m_page = 0;
		uint8_t m_column = 0;
		uint8_t m_startLine = 0;
		uint8_t m_volume = 0x20;
		bool m_displayOn = false;
		bool m_adcReverse = false;
		bool m_comReverse = false;
		bool m_inverse = false;
		bool m_allOn = false;
		// second byte of a two-byte command in flight: 0x81 or 0xAC/0xAD
		uint8_t m_pendingCommand = 0;
		uint64_t m_revision = 0;
	};
}

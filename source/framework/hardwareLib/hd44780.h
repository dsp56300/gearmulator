#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace hwLib
{
	// Hitachi HD44780 character-LCD controller and its many compatibles
	// (ST7066, KS0066, SPLC780 and the modules built around them:
	// EW20290GLW, NHD-0220DZW-AB5, ...).
	//
	// The controller always has 80 bytes of DDRAM -- addresses 0x00-0x27 for
	// line 0 and 0x40-0x67 for line 1 -- regardless of how wide the glass in
	// front of it is. How much of that is actually visible, and where the
	// visible window starts, is a property of the module and of the display
	// shift, not of the silicon. So storage here is always the full 80 cells
	// and the window is applied on the way out, by getVisibleCharacter() and
	// copyVisibleDdRam().
	//
	// Display shift moves the window, it does not move DDRAM. A character
	// scrolled out of sight is still in the controller and comes back when the
	// display shifts the other way.
	class Hd44780
	{
	public:
		// Fixed by the silicon.
		static constexpr uint32_t Columns    = 40;	// DDRAM cells per line
		static constexpr uint32_t Lines      = 2;
		static constexpr uint32_t DdRamSize  = Columns * Lines;
		static constexpr uint32_t CgRamSize  = 64;	// 8 characters x 8 rows, 5 bits each

		using ChangeCallback = std::function<void()>;

		// _visibleColumns / _visibleLines describe the glass, not the chip.
		explicit Hd44780(uint32_t _visibleColumns = 20, uint32_t _visibleLines = 2);

		void reset();

		// One bus cycle. _registerSelect false is the instruction register, true
		// the data register. Returns a value only for reads.
		std::optional<uint8_t> exec(bool _registerSelect, bool _read, uint8_t _data);

		// Write-only convenience for the boards that never read the controller.
		void write(const bool _registerSelect, const uint8_t _data)
		{
			exec(_registerSelect, false, _data);
		}

		// Raw controller state, all 80 / 64 cells, unaffected by the window.
		const std::array<uint8_t, DdRamSize>& getDdRam() const { return m_ddRam; }
		const std::array<uint8_t, CgRamSize>& getCgRam() const { return m_cgRam; }
		// Monotonic presentation revision. Consumers can poll this inexpensive
		// scalar and copy the RAM only after something visible has changed.
		uint64_t getContentGeneration() const { return m_contentGeneration; }

		// Raw cell at (line, column), column being a DDRAM column 0..39.
		uint8_t getCharacter(uint32_t _line, uint32_t _column) const;

		// What the glass shows at (line, column), with the display shift applied.
		// _column is 0..getVisibleColumns()-1.
		uint8_t getVisibleCharacter(uint32_t _line, uint32_t _column) const;

		// The whole visible window, row-major, getVisibleColumns() *
		// getVisibleLines() characters. _dst must have room for that many.
		void copyVisibleDdRam(char* _dst) const;

		// The eight rows (5 bits each, bit 4 = leftmost pixel) of user-defined
		// character 0..7.
		std::array<uint8_t, 8> getCgCharacter(uint32_t _index) const;
		bool getCgData(std::array<uint8_t, 8>& _data, uint32_t _charIndex) const;

		uint32_t getVisibleColumns() const { return m_visibleColumns; }
		uint32_t getVisibleLines() const { return m_visibleLines; }

		// Column of DDRAM currently shown in the leftmost visible cell.
		uint32_t getDisplayShift() const { return m_displayShiftOffset; }

		uint32_t getCursorAddress() const { return m_ddAddr; }
		bool isCursorOn() const { return m_cursorOn; }
		bool isCursorBlinking() const { return m_cursorBlinking; }
		bool isDisplayOn() const { return m_displayOn; }
		bool isTwoLineMode() const { return m_twoLine; }

		// Shim for panels whose firmware writes past the visible window and
		// expects it to follow, rather than issuing a display shift. When set,
		// a DDRAM write moves the window by the smallest amount that brings
		// the written cell back into view; DDRAM itself is never modified.
		void setAutoFollow(const bool _autoFollow) { m_autoFollow = _autoFollow; }
		bool getAutoFollow() const { return m_autoFollow; }

		void setChangeCallback(const ChangeCallback& _callback) { m_changeCallback = _callback; }
		void setCgRamChangeCallback(const ChangeCallback& _callback) { m_cgRamChangeCallback = _callback; }
		void setCursorChangeCallback(const ChangeCallback& _callback) { m_cursorChangeCallback = _callback; }

	private:
		// DDRAM address (0x00-0x27, 0x40-0x67) -> flat index into m_ddRam.
		// Returns false for the gaps 0x28-0x3f and 0x68-0x7f, which the
		// controller has no RAM for.
		bool ddRamIndex(uint8_t _addr, uint32_t& _index) const;

		void advanceDdAddr();
		void followWindow(uint8_t _addr);

		std::array<uint8_t, DdRamSize> m_ddRam{};
		std::array<uint8_t, CgRamSize> m_cgRam{};

		uint32_t m_visibleColumns;
		uint32_t m_visibleLines;

		uint8_t m_ddAddr = 0;		// DDRAM address counter
		uint8_t m_cgAddr = 0;		// CGRAM address counter
		bool m_cgRamMode = false;	// data register targets CGRAM instead of DDRAM

		uint32_t m_displayShiftOffset = 0;	// leftmost visible DDRAM column

		bool m_increment    = true;		// I/D
		bool m_shiftOnWrite = false;	// S, entry mode
		bool m_displayOn    = false;	// D
		bool m_cursorOn     = false;	// C
		bool m_cursorBlinking = false;	// B
		bool m_dataLength8  = true;		// DL
		bool m_twoLine      = false;	// N
		bool m_font5x10     = false;	// F

		bool m_autoFollow = false;
		uint64_t m_contentGeneration = 0;

		ChangeCallback m_changeCallback;
		ChangeCallback m_cgRamChangeCallback;
		ChangeCallback m_cursorChangeCallback;
	};
}

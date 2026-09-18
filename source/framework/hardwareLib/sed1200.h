// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Gearmulator contributors
#pragma once

#include "hd44780.h"

#include <array>
#include <cstdint>
#include <functional>

namespace hwLib
{
	class Sed1200
	{
	public:
		using ChangeCallback = std::function<void()>;
		void reset();
		void writeControl(uint8_t _data);
		void writeData(uint8_t _data);
		const std::array<uint8_t, 80>& getDdRam() const { return m_lcd.getDdRam(); }
		const std::array<uint8_t, 64>& getCgRam() const { return m_lcd.getCgRam(); }
		// One strip of 20 cells, whichever of the controller's line modes is in force: the
		// second line of a two-line layout occupies cells 10-19 of the same strip.
		uint32_t getVisibleColumns() const { return m_lcd.getVisibleColumns(); }
		uint8_t getVisibleCharacter(uint32_t _column) const { return m_lcd.getVisibleCharacter(0, _column); }
		std::array<uint8_t, 8> getCgCharacter(uint32_t _index) const { return m_lcd.getCgCharacter(_index); }
		uint8_t getCursorAddress() const { return m_cursorAddress; }
		bool isCursorOn() const { return m_lcd.isCursorOn(); }
		bool isCursorBlinking() const { return m_lcd.isCursorBlinking(); }
		bool isDisplayOn() const { return m_lcd.isDisplayOn(); }
		void setChangeCallback(ChangeCallback _callback);

	private:
		void stepCursor();
		void syncCursorAddress();
		void syncDisplayControl();
		Hd44780 m_lcd{20, 1};
		ChangeCallback m_changeCallback;
		uint8_t m_cursorAddress = 0;
		uint8_t m_cgAddress = 0;
		bool m_cursorDecrement = false;
		bool m_cursorBlinking = false;
		bool m_cursorOn = false;
		bool m_displayOn = false;
		bool m_twoLines = false;
	};
}

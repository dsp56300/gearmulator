// license:BSD-3-Clause
// Based on MAME's SED1200 implementation by Olivier Galibert.
#include "sed1200.h"

#include <algorithm>
#include <utility>

namespace hwLib
{
	void Sed1200::reset()
	{
		m_lcd.reset();
		m_cursorAddress = m_cgAddress = 0;
		m_cursorDecrement = m_cursorBlinking = m_cursorOn = m_displayOn = m_twoLines = false;
		if (m_changeCallback) m_changeCallback();
	}

	void Sed1200::setChangeCallback(ChangeCallback _callback)
	{
		m_changeCallback = std::move(_callback);
		m_lcd.setChangeCallback(m_changeCallback);
		m_lcd.setCgRamChangeCallback(m_changeCallback);
		m_lcd.setCursorChangeCallback(m_changeCallback);
	}

	void Sed1200::syncCursorAddress()
	{
		m_lcd.write(false, uint8_t(0x80 | m_cursorAddress));
	}

	void Sed1200::syncDisplayControl()
	{
		m_lcd.write(false, uint8_t(0x08 | (m_displayOn ? 4 : 0) | (m_cursorOn ? 2 : 0) | (m_cursorBlinking ? 1 : 0)));
	}

	void Sed1200::stepCursor()
	{
		if (m_cursorDecrement)
		{
			if (m_cursorAddress != 0 && (!m_twoLines || m_cursorAddress != 10)) --m_cursorAddress;
		}
		else if ((!m_twoLines || m_cursorAddress != 9) && m_cursorAddress != 19) ++m_cursorAddress;
	}

	void Sed1200::writeData(const uint8_t _data)
	{
		syncCursorAddress();
		m_lcd.write(true, _data);
		stepCursor();
		syncCursorAddress();
	}

	void Sed1200::writeControl(const uint8_t _data)
	{
		switch (_data)
		{
		case 0x04: case 0x05: m_cursorDecrement = (_data & 1) != 0; break;
		case 0x06: case 0x07: stepCursor(); syncCursorAddress(); break;
		case 0x0a: case 0x0b: m_cursorBlinking = (_data & 1) != 0; syncDisplayControl(); break;
		case 0x0c: case 0x0d: m_displayOn = (_data & 1) != 0; syncDisplayControl(); break;
		case 0x0e: case 0x0f: m_cursorOn = (_data & 1) != 0; syncDisplayControl(); break;
		case 0x10:
			reset();
			break;
		case 0x12: case 0x13: m_twoLines = (_data & 1) != 0; break;
		default:
			if ((_data & 0xf0) == 0x20) m_cgAddress = uint8_t((_data & 3) * 8);
			else if ((_data & 0xe0) == 0x40)
			{
				m_lcd.write(false, uint8_t(0x40 | m_cgAddress));
				m_lcd.write(true, uint8_t(_data & 0x1f));
				++m_cgAddress;
				m_cgAddress &= 31;
			}
			else if (_data & 0x80)
			{
				if (m_twoLines)
				{
					m_cursorAddress = (_data & 0x40) ? 10 : 0;
					m_cursorAddress += std::min<uint8_t>(_data & 0x3f, 9);
				}
				else m_cursorAddress = std::min<uint8_t>(_data & 0x3f, 19);
				syncCursorAddress();
			}
			break;
		}
		if (m_changeCallback) m_changeCallback();
	}
}

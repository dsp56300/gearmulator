#include "hd44780.h"

namespace hwLib
{
	namespace
	{
		// DDRAM address ranges. Two-line mode splits the 80 cells into
		// 0x00-0x27 and 0x40-0x67; one-line mode is a single 0x00-0x4f run.
		constexpr uint8_t g_line0First = 0x00;
		constexpr uint8_t g_line0Last  = 0x27;
		constexpr uint8_t g_line1First = 0x40;
		constexpr uint8_t g_line1Last  = 0x67;
		constexpr uint8_t g_oneLineLast = 0x4f;
	}

	Hd44780::Hd44780(const uint32_t _visibleColumns, const uint32_t _visibleLines)
		: m_visibleColumns(_visibleColumns ? _visibleColumns : Columns)
		, m_visibleLines(_visibleLines ? _visibleLines : Lines)
	{
		if(m_visibleColumns > Columns)
			m_visibleColumns = Columns;
		if(m_visibleLines > Lines)
			m_visibleLines = Lines;

		reset();
	}

	void Hd44780::reset()
	{
		m_ddRam.fill(' ');
		m_cgRam.fill(0);

		m_ddAddr = 0;
		m_cgAddr = 0;
		m_cgRamMode = false;
		m_displayShiftOffset = 0;

		m_increment = true;
		m_shiftOnWrite = false;
		m_displayOn = false;
		m_cursorOn = false;
		m_cursorBlinking = false;
		m_dataLength8 = true;
		m_twoLine = false;
		m_font5x10 = false;
		++m_contentGeneration;
	}

	bool Hd44780::ddRamIndex(const uint8_t _addr, uint32_t& _index) const
	{
		if(m_twoLine)
		{
			if(_addr <= g_line0Last)
			{
				_index = _addr;
				return true;
			}
			if(_addr >= g_line1First && _addr <= g_line1Last)
			{
				_index = Columns + static_cast<uint32_t>(_addr - g_line1First);
				return true;
			}
			// 0x28-0x3f and 0x68-0x7f have no RAM behind them
			return false;
		}

		if(_addr <= g_oneLineLast)
		{
			_index = _addr;
			return true;
		}
		return false;
	}

	void Hd44780::advanceDdAddr()
	{
		if(m_twoLine)
		{
			if(m_increment)
			{
				if(m_ddAddr == g_line0Last)			m_ddAddr = g_line1First;
				else if(m_ddAddr == g_line1Last)	m_ddAddr = g_line0First;
				else								++m_ddAddr;
			}
			else
			{
				if(m_ddAddr == g_line0First)		m_ddAddr = g_line1Last;
				else if(m_ddAddr == g_line1First)	m_ddAddr = g_line0Last;
				else								--m_ddAddr;
			}
			return;
		}

		if(m_increment)
			m_ddAddr = (m_ddAddr >= g_oneLineLast) ? g_line0First : static_cast<uint8_t>(m_ddAddr + 1);
		else
			m_ddAddr = (m_ddAddr == g_line0First) ? g_oneLineLast : static_cast<uint8_t>(m_ddAddr - 1);
	}

	void Hd44780::followWindow(const uint8_t _addr)
	{
		uint32_t index;
		if(!ddRamIndex(_addr, index))
			return;

		const auto column = index % Columns;
		const auto relative = (column + Columns - m_displayShiftOffset) % Columns;

		if(relative < m_visibleColumns)
			return;	// already in view

		// Bring it in with the smaller of the two possible window movements:
		// scroll right until it is the last visible cell, or left until it is
		// the first.
		const auto offsetRight = (column + Columns - (m_visibleColumns - 1)) % Columns;
		const auto moveRight = (offsetRight + Columns - m_displayShiftOffset) % Columns;

		const auto offsetLeft = column;
		const auto moveLeft = (m_displayShiftOffset + Columns - offsetLeft) % Columns;

		m_displayShiftOffset = (moveRight <= moveLeft) ? offsetRight : offsetLeft;
	}

	std::optional<uint8_t> Hd44780::exec(const bool _registerSelect, const bool _read, const uint8_t _data)
	{
		const auto prevDisplayOn      = m_displayOn;
		const auto prevCursorOn       = m_cursorOn;
		const auto prevCursorBlinking = m_cursorBlinking;
		const auto prevDdAddr         = m_ddAddr;
		const auto prevShift          = m_displayShiftOffset;

		bool changed = false;
		bool cgRamChanged = false;
		bool contentChanged = false;

		std::optional<uint8_t> result;

		if(_read)
		{
			if(_registerSelect)
			{
				// Read data from CGRAM or DDRAM, then post inc/dec the counter.
				if(m_cgRamMode)
				{
					result = m_cgRam[m_cgAddr & (CgRamSize - 1)];
					m_cgAddr = static_cast<uint8_t>((m_cgAddr + (m_increment ? 1 : -1)) & (CgRamSize - 1));
				}
				else
				{
					uint32_t index;
					result = ddRamIndex(m_ddAddr, index) ? m_ddRam[index] : static_cast<uint8_t>(' ');
					advanceDdAddr();
				}
			}
			else
			{
				// Busy flag is never set -- we do not model instruction timing --
				// so this is just the address counter.
				const uint8_t ac = m_cgRamMode
					? static_cast<uint8_t>(m_cgAddr & 0x3f)
					: static_cast<uint8_t>(m_ddAddr & 0x7f);
				result = static_cast<uint8_t>(ac & 0x7f);
			}
		}
		else if(!_registerSelect)
		{
			// ---- Instruction register. Tested most-specific first. ----
			if(_data == 0x01)								// Clear display
			{
				for(const auto cell : m_ddRam)
					contentChanged = contentChanged || cell != ' ';
				contentChanged = contentChanged || m_displayShiftOffset != 0;
				m_ddRam.fill(' ');
				m_ddAddr = 0;
				// Both this and Return home load the address counter with a DDRAM address, so like
				// Set DDRAM address they take the data register back out of CGRAM. Without it,
				// define glyphs -> clear -> write text put the text into CGRAM.
				m_cgRamMode = false;
				m_displayShiftOffset = 0;
				m_increment = true;
				changed = true;
			}
			else if((_data & 0xfe) == 0x02)					// Return home
			{
				m_ddAddr = 0;
				m_cgRamMode = false;
				m_displayShiftOffset = 0;
			}
			else if((_data & 0xfc) == 0x04)					// Entry mode set
			{
				m_increment    = (_data & 0x02) != 0;
				m_shiftOnWrite = (_data & 0x01) != 0;
			}
			else if((_data & 0xf8) == 0x08)					// Display on/off control
			{
				m_displayOn      = (_data & 0x04) != 0;
				m_cursorOn       = (_data & 0x02) != 0;
				m_cursorBlinking = (_data & 0x01) != 0;
				changed = true;
			}
			else if((_data & 0xf0) == 0x10)					// Cursor / display shift
			{
				const bool shiftDisplay = (_data & 0x08) != 0;
				const bool toTheRight   = (_data & 0x04) != 0;

				if(shiftDisplay)
				{
					// Shifting the display right moves the content right, which
					// means the window starts one cell earlier.
					m_displayShiftOffset = toTheRight
						? (m_displayShiftOffset + Columns - 1) % Columns
						: (m_displayShiftOffset + 1) % Columns;
				}
				else
				{
					const auto inc = m_increment;
					m_increment = toTheRight;
					advanceDdAddr();
					m_increment = inc;
				}
			}
			else if((_data & 0xe0) == 0x20)					// Function set
			{
				m_dataLength8 = (_data & 0x10) != 0;
				m_twoLine     = (_data & 0x08) != 0;
				m_font5x10    = (_data & 0x04) != 0;
			}
			else if((_data & 0xc0) == 0x40)					// Set CGRAM address
			{
				m_cgAddr = _data & 0x3f;
				m_cgRamMode = true;
			}
			// 0b1AAAAAAA. Masked rather than left as the trailing else, because that swallowed
			// instruction 0x00 - undefined on the real controller - and firmware idling with it
			// between Set CGRAM address and its data silently reset the counter out of CGRAM.
			else if(_data & 0x80)							// Set DDRAM address
			{
				m_ddAddr = _data & 0x7f;
				m_cgRamMode = false;
			}
		}
		else
		{
			// ---- Data register ----
			if(m_cgRamMode)
			{
				if(m_cgRam[m_cgAddr] != (_data & 0x1f))
				{
					m_cgRam[m_cgAddr] = _data & 0x1f;
					cgRamChanged = true;
					contentChanged = true;
				}
				m_cgAddr = static_cast<uint8_t>((m_cgAddr + (m_increment ? 1 : -1)) & (CgRamSize - 1));
			}
			else
			{
				if(m_autoFollow)
					followWindow(m_ddAddr);

				uint32_t index;
				if(ddRamIndex(m_ddAddr, index) && m_ddRam[index] != _data)
				{
					m_ddRam[index] = _data;
					changed = true;
					contentChanged = true;
				}

				advanceDdAddr();

				if(m_shiftOnWrite)
				{
					m_displayShiftOffset = m_increment
						? (m_displayShiftOffset + 1) % Columns
						: (m_displayShiftOffset + Columns - 1) % Columns;
				}
			}
		}

		if(m_displayShiftOffset != prevShift)
			changed = true;
		if(m_displayShiftOffset != prevShift || m_displayOn != prevDisplayOn)
			contentChanged = true;

		if(contentChanged)
			++m_contentGeneration;

		if(changed && m_changeCallback)
			m_changeCallback();

		if(cgRamChanged && m_cgRamChangeCallback)
			m_cgRamChangeCallback();

		if(m_cursorChangeCallback &&
			(prevDisplayOn      != m_displayOn      ||
			 prevCursorOn       != m_cursorOn       ||
			 prevCursorBlinking != m_cursorBlinking ||
			 prevDdAddr         != m_ddAddr))
			m_cursorChangeCallback();

		return result;
	}

	uint8_t Hd44780::getCharacter(const uint32_t _line, const uint32_t _column) const
	{
		if(_line >= Lines || _column >= Columns)
			return ' ';
		return m_ddRam[_line * Columns + _column];
	}

	uint8_t Hd44780::getVisibleCharacter(const uint32_t _line, const uint32_t _column) const
	{
		if(_line >= m_visibleLines || _column >= m_visibleColumns)
			return ' ';

		if(!m_twoLine)
			return m_ddRam[(m_displayShiftOffset + _column) % DdRamSize];

		return getCharacter(_line, (m_displayShiftOffset + _column) % Columns);
	}

	void Hd44780::copyVisibleDdRam(char* _dst) const
	{
		if(!_dst)
			return;

		for(uint32_t line = 0; line < m_visibleLines; ++line)
		{
			for(uint32_t col = 0; col < m_visibleColumns; ++col)
				*_dst++ = static_cast<char>(getVisibleCharacter(line, col));
		}
	}

	std::array<uint8_t, 8> Hd44780::getCgCharacter(const uint32_t _index) const
	{
		std::array<uint8_t, 8> rows{};
		if(_index >= 8)
			return rows;
		for(uint32_t r = 0; r < 8; ++r)
			rows[r] = m_cgRam[_index * 8 + r];
		return rows;
	}

	bool Hd44780::getCgData(std::array<uint8_t, 8>& _data, const uint32_t _charIndex) const
	{
		if(_charIndex >= 8)
			return false;
		_data = getCgCharacter(_charIndex);
		return true;
	}
}

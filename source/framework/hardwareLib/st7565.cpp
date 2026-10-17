#include "st7565.h"

#include <algorithm>

namespace hwLib
{
	St7565::St7565(const uint32_t _panelWidth, const bool _segmentsReversed)
		: m_width(std::min(_panelWidth, Columns)), m_segmentsReversed(_segmentsReversed)
	{
		reset();
	}

	void St7565::reset()
	{
		m_ram.fill(0);
		m_page = 0;
		m_column = 0;
		m_startLine = 0;
		m_volume = 0x20;
		m_displayOn = false;
		m_adcReverse = false;
		m_comReverse = false;
		m_inverse = false;
		m_allOn = false;
		m_pendingCommand = 0;
		++m_revision;
	}

	void St7565::writeCommand(const uint8_t _cmd)
	{
		if(m_pendingCommand)
		{
			if(m_pendingCommand == 0x81) { m_volume = _cmd & 0x3f; ++m_revision; }
			m_pendingCommand = 0;
			return;
		}
		switch(_cmd & 0xf0)
		{
		case 0x00: m_column = static_cast<uint8_t>((m_column & 0xf0) | (_cmd & 0x0f)); return;
		case 0x10: m_column = static_cast<uint8_t>((m_column & 0x0f) | ((_cmd & 0x0f) << 4)); return;
		case 0x40: case 0x50: case 0x60: case 0x70:
			m_startLine = _cmd & 0x3f; ++m_revision; return;
		case 0xb0: m_page = _cmd & 0x0f; return;
		case 0x20: return;  // V0 resistor ratio (0x20-0x27) / power control (0x28-0x2f)
		default: break;
		}
		switch(_cmd)
		{
		case 0x81: m_pendingCommand = 0x81; return;  // electronic volume, value follows
		case 0xa0: m_adcReverse = false; ++m_revision; return;
		case 0xa1: m_adcReverse = true; ++m_revision; return;
		case 0xa2: case 0xa3: return;  // bias
		case 0xa4: m_allOn = false; ++m_revision; return;
		case 0xa5: m_allOn = true; ++m_revision; return;
		case 0xa6: m_inverse = false; ++m_revision; return;
		case 0xa7: m_inverse = true; ++m_revision; return;
		case 0xac: case 0xad: m_pendingCommand = _cmd; return;  // static indicator, mode byte follows
		case 0xae: m_displayOn = false; ++m_revision; return;
		case 0xaf: m_displayOn = true; ++m_revision; return;
		case 0xc0: m_comReverse = false; ++m_revision; return;
		case 0xc8: m_comReverse = true; ++m_revision; return;
		case 0xe2: reset(); return;
		case 0xe3: return;  // NOP
		case 0xf8: m_pendingCommand = 0xf8; return;  // booster ratio, value follows
		default: return;
		}
	}

	void St7565::writeData(const uint8_t _value)
	{
		if(m_page >= Pages || m_column >= Columns) return;
		m_ram[m_page * Columns + m_column] = _value;
		if(m_column < Columns - 1) ++m_column;
		++m_revision;
	}

	uint8_t St7565::readData()
	{
		if(m_page >= Pages || m_column >= Columns) return 0;
		const uint8_t v = m_ram[m_page * Columns + m_column];
		if(m_column < Columns - 1) ++m_column;
		return v;
	}

	void St7565::renderMono(std::vector<uint8_t>& _out) const
	{
		_out.assign(m_width * Rows, 0);
		if(!m_displayOn) return;
		for(uint32_t y = 0; y < Rows; ++y)
		{
			const uint32_t row = (y + m_startLine) & (Rows - 1);
			const uint32_t page = row >> 3, bit = row & 7;
			const uint32_t outY = m_comReverse ? Rows - 1 - y : y;
			for(uint32_t x = 0; x < m_width; ++x)
			{
				// panel column -> segment output -> RAM column
				const uint32_t segment = m_segmentsReversed ? Columns - 1 - x : x;
				const uint32_t column = m_adcReverse ? Columns - 1 - segment : segment;
				bool on = (m_ram[page * Columns + column] >> bit) & 1;
				if(m_inverse) on = !on;
				if(m_allOn) on = true;
				_out[outY * m_width + x] = on ? 1 : 0;
			}
		}
	}
}

#include "88lib/boards/sc880Lcd.h"

namespace emu88Lib
{
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

	void Sc880Lcd::renderMono(std::vector<uint8_t>& _output) const
	{
		_output.assign(Width * Height, 0);
		for(uint32_t y = 0; y < Height; ++y)
		{
			for(uint32_t x = 0; x < Width; ++x)
				_output[y * Width + x] = (m_ram[y * Columns + x / 8] >> (7 - x % 8)) & 1;
		}
	}
}

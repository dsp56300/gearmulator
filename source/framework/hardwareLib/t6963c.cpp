#include "t6963c.h"

#include "lcdfonts.h"

#include <algorithm>

namespace hwLib
{
	T6963C::T6963C(const uint32_t _width, const uint32_t _height, std::vector<uint8_t> _cgRom)
		: m_width(_width), m_height(_height), m_cgRom(std::move(_cgRom))
	{
		reset();
	}

	void T6963C::reset()
	{
		std::fill(m_memory.begin(), m_memory.end(), 0);
		m_data = m_address = m_graphicHome = m_textHome = m_graphicArea = m_textArea = m_cgRamOffset = 0;
		m_mode = m_displayMode = m_readData = 0;
		m_autoMode = AutoMode::None;
		m_dirty = true;
	}

	uint8_t T6963C::read(const uint16_t _offset)
	{
		if (_offset & 1) return 0x2f;	// STA0-3 (command, data, auto read, auto write ready) and STA5
		if (m_autoMode == AutoMode::Read)
		{
			const uint8_t value = m_memory[m_address++ & 0x7fff];
			return value;
		}
		return m_readData;
	}

	void T6963C::write(const uint16_t _offset, const uint8_t _data)
	{
		if (_offset & 1)
		{
			command(_data);
			return;
		}
		if (m_autoMode == AutoMode::Write)
		{
			m_memory[m_address++ & 0x7fff] = _data;
			changed();
			return;
		}
		m_data = static_cast<uint16_t>((uint16_t(_data) << 8) | (m_data >> 8));
	}

	void T6963C::command(const uint8_t _command)
	{
		if ((_command & 0xf0) == 0x20)
		{
			if ((_command & 0x0f) == 0x02) m_cgRamOffset = static_cast<uint16_t>((m_data & 0x1f) << 11);
			else if ((_command & 0x0f) == 0x04) m_address = m_data;
		}
		else if ((_command & 0xfd) == 0x40)
		{
			if (_command & 2) m_graphicHome = m_data; else m_textHome = m_data;
		}
		else if ((_command & 0xfd) == 0x41)
		{
			if (_command & 2) m_graphicArea = m_data; else m_textArea = m_data;
		}
		else if ((_command & 0xf0) == 0x80) m_mode = _command & 0x0f;
		else if ((_command & 0xf0) == 0x90) { m_displayMode = _command & 0x0f; changed(); }
		else if ((_command & 0xfe) == 0xb0) m_autoMode = (_command & 1) ? AutoMode::Read : AutoMode::Write;
		else if ((_command & 0xfe) == 0xb2) m_autoMode = AutoMode::None;	// auto reset is 1011001x
		else if ((_command & 0xf0) == 0xc0)
		{
			if (_command & 1) m_readData = m_memory[m_address & 0x7fff];
			else { m_memory[m_address & 0x7fff] = static_cast<uint8_t>(m_data >> 8); changed(); }
			if ((_command & 0x0e) == 0) ++m_address;
			else if ((_command & 0x0e) == 2) --m_address;
		}
		else if ((_command & 0xf0) == 0xf0)
		{
			auto& value = m_memory[m_address & 0x7fff];
			if (_command & 8) value |= static_cast<uint8_t>(1u << (_command & 7));
			else value &= static_cast<uint8_t>(~(1u << (_command & 7)));
			changed();
		}
	}

	void T6963C::renderMono(std::vector<uint8_t>& _output) const
	{
		_output.assign(static_cast<size_t>(m_width) * m_height, 0);
		const unsigned lines = m_height / 8;
		if ((m_displayMode & 4) && m_textArea)
		{
			uint16_t address = m_textHome;
			for (unsigned y = 0; y < lines; ++y)
				for (unsigned x = 0; x < m_textArea; ++x)
				{
					const uint8_t character = m_memory[address++ & 0x7fff];
					for (unsigned cy = 0; cy < 8; ++cy)
					{
						uint8_t bits = 0;
						if (!(m_mode & 8) && character < 0x80)
						{
							if (m_cgRom.size() >= 0x400) bits = m_cgRom[character * 8 + cy];
							else if (cy < 7)
							{
								const uint8_t ascii = m_internalCgRom ? static_cast<uint8_t>(character + 0x20) : character;
								bits = static_cast<uint8_t>(getCharacterData(ascii)[cy] << 1);
							}
						}
						else
						{
							const uint16_t glyph = (m_mode & 8) ? character : (character & 0x7f);
							bits = m_memory[(m_cgRamOffset + glyph * 8 + cy) & 0x7fff];
						}
						for (unsigned cx = 0; cx < m_fontSize; ++cx)
						{
							const unsigned px = x * m_fontSize + cx;
							const unsigned py = y * 8 + cy;
							if (px < m_width && py < m_height)
								_output[py * m_width + px] = (bits >> (m_fontSize - 1 - cx)) & 1;
						}
					}
				}
		}
		if ((m_displayMode & 8) && m_graphicArea)
		{
			uint16_t address = m_graphicHome;
			for (unsigned y = 0; y < m_height; ++y)
				for (unsigned x = 0; x < m_graphicArea; ++x)
				{
					const uint8_t bits = m_memory[address++ & 0x7fff];
					for (unsigned bit = 0; bit < m_fontSize; ++bit)
					{
						const unsigned px = x * m_fontSize + bit;
						if (px >= m_width) continue;
						const uint8_t pixel = (bits >> (m_fontSize - 1 - bit)) & 1;
						auto& destination = _output[y * m_width + px];
						switch (m_mode & 7)
						{
						case 0: destination |= pixel; break;
						case 1: destination ^= pixel; break;
						case 3: destination &= pixel; break;
						default: break;
						}
					}
				}
		}
	}

	void T6963C::changed() { m_dirty = true; }
	void T6963C::flush()
	{
		if (!m_dirty) return;
		m_dirty = false;
		evChanged();
	}
}

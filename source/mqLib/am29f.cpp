#include "am29f.h"

#include <cassert>

Am29f::Am29f(uint8_t* _buffer, const size_t _size, bool _useWriteEnable, bool _bitreversedCmdAddr): m_buffer(_buffer), m_size(_size), m_useWriteEnable(_useWriteEnable), m_bitreverseCmdAddr(_bitreversedCmdAddr)
{
	auto br = [&](uint16_t x)
	{
		return m_bitreverseCmdAddr ? static_cast<uint16_t>(bitreverse(x) >> 4) : x;
	};

	// Chip Erase
	m_commands.push_back({{{br(0x555),0xAA}, {br(0x2AA),0x55}, {br(0x555),0x80}, {br(0x555),0xAA}, {br(0x2AA),0x55}, {br(0x555),0x10}}});

	// Sector Erase
	m_commands.push_back({{{br(0x555),0xAA}, {br(0x2AA),0x55}, {br(0x555),0x80}, {br(0x555),0xAA}, {br(0x2AA),0x55}}});

	// Program
	m_commands.push_back({{{br(0x555),0xAA}, {br(0x2AA),0x55}, {br(0x555),0xA0}}});
}

void Am29f::write(const uint32_t _addr, const uint16_t _data)
{
	const auto reset = [this]()
	{
		m_currentBusCycle = 0;
		m_currentCommand = -1;
	};

	if(!writeEnabled())
	{
		reset();
		return;
	}

	bool anyMatch = false;

	const auto d = _data & 0xff;
	
	for (size_t i=0; i<m_commands.size(); ++i)
	{
		auto& cycles = m_commands[i].cycles;

		if(m_currentBusCycle < cycles.size())
		{
			const auto& c = cycles[m_currentBusCycle];

			if(c.addr == _addr && c.data == d)
			{
				anyMatch = true;

				if(m_currentBusCycle == cycles.size() - 1)
					m_currentCommand = static_cast<int32_t>(i);
			}
		}
	}

	if(!anyMatch)
	{
		if(m_currentCommand >= 0)
		{
			const auto c = static_cast<CommandType>(m_currentCommand);

			execCommand(c, _addr, _data);
		}

		reset();
	}
	else
	{
		++m_currentBusCycle;
	}
}

void Am29f::execCommand(const CommandType _command, const uint32_t _addr, const uint16_t _data)
{
	switch (_command)
	{
	case CommandType::ChipErase:
		assert(false);
		break;
	case CommandType::SectorErase: 
		assert(false);
		break;
	case CommandType::Program:
		{
			if(_addr >= m_size)
				return;
			uint8_t* p = &m_buffer[_addr];
			auto* p16 = reinterpret_cast<uint16_t*>(p);
			*p16 = _data;
			break;
		}
	case CommandType::Invalid: 
	default: 
		assert(false);
		break;
	}
}

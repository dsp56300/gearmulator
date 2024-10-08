#include "am29f.h"

#include <cassert>

#include "mc68k/logging.h"
#include "mc68k/mc68k.h"

namespace hwLib
{
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
		const auto a = _addr & 0xfff;
		
		for (size_t i=0; i<m_commands.size(); ++i)
		{
			auto& cycles = m_commands[i].cycles;

			if(m_currentBusCycle < cycles.size())
			{
				const auto& c = cycles[m_currentBusCycle];

				if(c.addr == a && c.data == d)
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

	void Am29f::execCommand(const CommandType _command, uint32_t _addr, const uint16_t _data)
	{
		switch (_command)
		{
		case CommandType::ChipErase:
			assert(false);
			break;
		case CommandType::SectorErase:
			{
				size_t sectorSizekB = 0;
				switch (_addr)
				{
					case 0x00000:	sectorSizekB = 16;	break;
					case 0x04000:
					case 0x06000:	sectorSizekB = 8;	break;
					case 0x08000:	sectorSizekB = 32;	break;
					case 0x10000:
					case 0x20000:
					case 0x30000:
					case 0x40000:
					case 0x50000:
					case 0x60000:
					case 0x70000:	sectorSizekB = 64;	break;
					case 0x78000:	
					case 0x7A000:	
					case 0x7C000:
						// mq sends erase commands for a flash with top boot block even though a chip with bottom boot block is installed
						_addr = 0x70000;
						sectorSizekB = 64;
						break;
				default:
					MCLOG("Unable to erase sector at " << MCHEX(_addr) << ", out of bounds!");
					return;
				}

				MCLOG("Erasing Sector at " << MCHEX(_addr) << ", size " << MCHEX(1024 * sectorSizekB));
				for(size_t i = _addr; i< _addr + sectorSizekB * 1024; ++i)
					m_buffer[i] = 0xff;
			}
			break;
		case CommandType::Program:
			{
				if(_addr >= m_size)
					return;
				MCLOG("Programming word at " << MCHEX(_addr) << ", value " << MCHEXN(_data, 4));
				const auto old = mc68k::Mc68k::readW(m_buffer, _addr);
				// "A bit cannot be programmed from a 0 back to a 1"
				const auto v = _data & old;
				mc68k::Mc68k::writeW(m_buffer, _addr, v);
	//			assert(v == _data);
				break;
			}
		case CommandType::Invalid: 
		default: 
			assert(false);
			break;
		}
	}
}
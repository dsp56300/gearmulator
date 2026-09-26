#include "am29f.h"

#include <cassert>

#include "mc68k/logging.h"
#include "mc68k/mc68k.h"

namespace hwLib
{
	Am29f::Am29f(uint8_t* _buffer, const size_t _size, const bool _useWriteEnable, const bool _bitreversedCmdAddr, const bool _byteWide/* = false*/)
		: m_buffer(_buffer), m_size(_size), m_useWriteEnable(_useWriteEnable), m_bitreverseCmdAddr(_bitreversedCmdAddr), m_byteWide(_byteWide)
		// the chip decodes A10-A0 in a command cycle, which accepts $555/$2AA and the JEDEC $5555/$2AAA. At $AAA/$554
		// the host's bit 0 is not one of them
		, m_cmdAddrMask(_bitreversedCmdAddr ? 0xffe : 0x7ff)
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

		// Autoselect
		m_commands.push_back({{{br(0x555),0xAA}, {br(0x2AA),0x55}, {br(0x555),0x90}}});

		// Unlock Bypass
		m_commands.push_back({{{br(0x555),0xAA}, {br(0x2AA),0x55}, {br(0x555),0x20}}});
	}

	bool Am29f::readAutoselect(const uint32_t _addr, uint8_t& _result) const
	{
		if(!m_autoselect)
			return false;

		// word mode: the ids are words, big endian on the host's bus, the high bytes are zero
		if(!m_byteWide && !(_addr & 1))
		{
			_result = 0;
			return true;
		}

		switch ((m_byteWide ? _addr : _addr >> 1) & 0xff)
		{
		case 0:		_result = m_manufacturerId;	break;
		case 1:		_result = m_deviceId;		break;
		default:	_result = 0;				break;	// sector protection status
		}
		return true;
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
		const auto a = _addr & m_cmdAddrMask;

		// in unlock bypass mode only two commands exist, both without unlock cycles and at any address:
		// $A0 followed by address and data programs, $90 followed by $00 leaves the mode
		if(m_unlockBypass)
		{
			const auto command = m_bypassCommand;
			m_bypassCommand = 0;

			if(command == 0xa0)
				execCommand(CommandType::Program, _addr, _data);
			else if(command == 0x90)
				m_unlockBypass = d != 0x00;
			else if(d == 0xa0 || d == 0x90)
				m_bypassCommand = static_cast<uint8_t>(d);
			return;
		}

		// the cycle after a complete program command is its data, whatever address and data it has. Matched against the
		// command sequences, data AA at the $555 command address passed for cycle 4 of an erase and was never programmed
		if(m_currentCommand == static_cast<int32_t>(CommandType::Program))
		{
			execCommand(CommandType::Program, _addr, _data);
			reset();
			return;
		}

		// reset command, also valid as last cycle of an aborted command sequence
		if(d == 0xf0)
		{
			m_autoselect = false;
			reset();
			return;
		}

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
			return;
		}

		// these commands have no trailing data cycle and take effect immediately
		if(m_currentCommand == static_cast<int32_t>(CommandType::Autoselect))
		{
			m_autoselect = true;
			reset();
			return;
		}

		if(m_currentCommand == static_cast<int32_t>(CommandType::UnlockBypass))
		{
			m_unlockBypass = true;
			reset();
			return;
		}

		if(m_currentCommand == static_cast<int32_t>(CommandType::ChipErase))
		{
			execCommand(CommandType::ChipErase, _addr, _data);
			reset();
			return;
		}

		++m_currentBusCycle;
	}

	bool Am29f::eraseSector(const uint32_t _addr, const size_t _sizeInKb) const
	{
		if (!_sizeInKb || _addr + _sizeInKb * 1024 > m_size)
			return false;

		MCLOG("Erasing Sector at " << MCHEX(_addr) << ", size " << MCHEX(1024 * _sizeInKb));

		for(size_t i = _addr; i< _addr + _sizeInKb * 1024; ++i)
			m_buffer[i] = 0xff;

		return true;
	}

	// Am29F010: eight 16 KB sectors
	bool Am29f::eraseSector1Mbit(const uint32_t _addr) const
	{
		if(_addr >= 0x20000)	return false;
		return eraseSector(_addr & ~0x3fffu, 16);
	}

	// Am29F200BT: three 64 KB sectors, then 32, 8, 8 and 16 KB
	bool Am29f::eraseSector2MbitTopBoot(const uint32_t _addr) const
	{
		if(_addr >= 0x40000)	return false;
		if(_addr >= 0x3c000)	return eraseSector(0x3c000, 16);
		if(_addr >= 0x3a000)	return eraseSector(0x3a000, 8);
		if(_addr >= 0x38000)	return eraseSector(0x38000, 8);
		if(_addr >= 0x30000)	return eraseSector(0x30000, 32);
		return eraseSector(_addr & ~0xffffu, 64);
	}

	// Am29F400BT: seven 64 KB sectors, then 32, 8, 8 and 16 KB
	bool Am29f::eraseSector4MbitTopBoot(const uint32_t _addr) const
	{
		if(_addr >= 0x80000)	return false;
		if(_addr >= 0x7c000)	return eraseSector(0x7c000, 16);
		if(_addr >= 0x7a000)	return eraseSector(0x7a000, 8);
		if(_addr >= 0x78000)	return eraseSector(0x78000, 8);
		if(_addr >= 0x70000)	return eraseSector(0x70000, 32);
		return eraseSector(_addr & ~0xffffu, 64);
	}

	// Am29LV800BB: 16, 8, 8 and 32 KB, then fifteen 64 KB sectors
	bool Am29f::eraseSector8MbitBottomBoot(const uint32_t _addr) const
	{
		if(_addr >= 0x100000)	return false;
		if(_addr >= 0x10000)	return eraseSector(_addr & ~0xffffu, 64);
		if(_addr >= 0x08000)	return eraseSector(0x08000, 32);
		if(_addr >= 0x06000)	return eraseSector(0x06000, 8);
		if(_addr >= 0x04000)	return eraseSector(0x04000, 8);
		return eraseSector(0, 16);
	}

	void Am29f::execCommand(const CommandType _command, uint32_t _addr, const uint16_t _data) const
	{
		switch (_command)
		{
		case CommandType::ChipErase:
			MCLOG("Chip erase");
			for(size_t i=0; i<m_size; ++i)
				m_buffer[i] = 0xff;
			break;
		case CommandType::SectorErase:
			{
				if (!eraseSector(_addr))
				{
					assert(false);
					MCLOG("Unable to erase sector at " << MCHEX(_addr) << ", unable to determine sector size!");
				}
			}
			break;
		case CommandType::Program:
			{
				// a word needs both of its bytes in the memory
				if(_addr >= m_size || (!m_byteWide && _addr + 1 >= m_size))
					return;

				if(m_byteWide)
				{
					// "A bit cannot be programmed from a 0 back to a 1"
					m_buffer[_addr] &= static_cast<uint8_t>(_data);
					break;
				}
#if defined(_DEBUG) && defined(_WIN32)
				MCLOG("Programming word at " << MCHEX(_addr) << ", value " << MCHEXN(_data, 4));
#endif
				const auto old = mc68k::memoryOps::readU16(m_buffer, _addr);
				// "A bit cannot be programmed from a 0 back to a 1"
				const auto v = _data & old;
				mc68k::memoryOps::writeU16(m_buffer, _addr, v);
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
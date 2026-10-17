#include "lh28fbj.h"

#include <algorithm>

namespace hwLib
{
	Lh28fBj::Lh28fBj(std::vector<uint8_t>& _array, const uint32_t _size, const uint16_t _device, const uint16_t _manufacturer)
		: m_array(_array), m_size(_size), m_manufacturer(_manufacturer), m_device(_device)
	{
		if(m_array.size() < m_size) m_array.resize(m_size, 0xff);
	}

	void Lh28fBj::reset()
	{
		m_mode = Mode::ReadArray;
		m_status = StatusReady;
	}

	uint32_t Lh28fBj::blockStart(const uint32_t _addr) const
	{
		const uint32_t a = _addr & (m_size - 1);
		return a & ~(blockSize(a) - 1);
	}

	uint32_t Lh28fBj::blockSize(const uint32_t _addr) const
	{
		return (_addr & (m_size - 1)) < ParameterBlocks * ParameterBlockSize ? ParameterBlockSize : MainBlockSize;
	}

	uint16_t Lh28fBj::arrayWord(const uint32_t _addr) const
	{
		const uint32_t a = _addr & (m_size - 2);
		return static_cast<uint16_t>((m_array[a] << 8) | m_array[a + 1]);
	}

	uint16_t Lh28fBj::read(const uint32_t _addr) const
	{
		switch(m_mode)
		{
		case Mode::ReadArray:
			return arrayWord(_addr);
		case Mode::ReadId:
			// word 0 manufacturer, word 1 device, word 2 the block's lock bit
			switch((_addr >> 1) & 3)
			{
			case 0: return m_manufacturer;
			case 1: return m_device;
			default: return 0;
			}
		default:
			return m_status;
		}
	}

	void Lh28fBj::write(const uint32_t _addr, const uint16_t _data)
	{
		const uint8_t command = static_cast<uint8_t>(_data);
		switch(m_mode)
		{
		case Mode::ProgramSetup:
		{
			// Programming only clears bits.
			const uint32_t a = _addr & (m_size - 2);
			m_array[a] &= static_cast<uint8_t>(_data >> 8);
			m_array[a + 1] &= static_cast<uint8_t>(_data);
			++m_programs;
			m_dirty = true;
			m_mode = Mode::ReadStatus;
			return;
		}
		case Mode::EraseSetup:
			if(command == 0xd0)
			{
				const uint32_t start = blockStart(_addr);
				std::fill(m_array.begin() + start, m_array.begin() + start + blockSize(_addr), 0xff);
				++m_erases;
				m_dirty = true;
			}
			else
				m_status |= StatusSequenceError;
			m_mode = Mode::ReadStatus;
			return;
		case Mode::LockSetup:
			// set (01) / clear (D0) block lock bits: accepted, not enforced
			m_mode = Mode::ReadStatus;
			return;
		default:
			break;
		}
		switch(command)
		{
		case 0xff: m_mode = Mode::ReadArray; return;
		case 0x90: m_mode = Mode::ReadId; return;
		case 0x70: m_mode = Mode::ReadStatus; return;
		case 0x50: m_status = StatusReady; m_mode = Mode::ReadStatus; return;
		case 0x40:
		case 0x10: m_mode = Mode::ProgramSetup; return;
		case 0x20: m_mode = Mode::EraseSetup; return;
		case 0x60: m_mode = Mode::LockSetup; return;
		case 0xb0:  // suspend: nothing is ever in progress
		case 0xd0:  // resume
			m_mode = Mode::ReadStatus;
			return;
		default:
			return;  // unrecognised command bytes are ignored
		}
	}
}

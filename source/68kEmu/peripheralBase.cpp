#include "peripheralBase.h"

#include "mc68k.h"

namespace mc68k
{
	PeripheralBase::PeripheralBase(const uint32_t _base, const uint32_t _size) : m_baseAddress(_base)
	{
		m_buffer.resize(_size, 0);
	}

	uint8_t PeripheralBase::read8(const PeriphAddress _addr)
	{
		return m_buffer[static_cast<size_t>(_addr) - m_baseAddress];
	}

	uint16_t PeripheralBase::read16(const PeriphAddress _addr)
	{
		return Mc68k::readW(m_buffer, static_cast<size_t>(_addr) - m_baseAddress);
	}

	void PeripheralBase::write8(PeriphAddress _addr, uint8_t _val)
	{
		m_buffer[static_cast<size_t>(_addr) - m_baseAddress] = _val;
	}

	void PeripheralBase::write16(PeriphAddress _addr, uint16_t _val)
	{
		return Mc68k::writeW(m_buffer, static_cast<size_t>(_addr) - m_baseAddress, _val);
	}
}

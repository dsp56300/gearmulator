#include "port.h"

namespace mc68k
{
	void Port::writeTX(uint8_t _data)
	{
		// only write pins that are enabled and that are set to output
		const auto mask = m_direction & m_enabledPins;
		m_data &= ~mask;
		m_data |= _data & mask;
		++m_writeCounter;
	}

	void Port::writeRX(uint8_t _data)
	{
		// only write pins that are enabled and that are set to input
		const auto mask = (~m_direction) & m_enabledPins;
		m_data &= ~mask;
		m_data |= _data & mask;
	}

	uint8_t Port::read() const
	{
		return m_data;
	}
}

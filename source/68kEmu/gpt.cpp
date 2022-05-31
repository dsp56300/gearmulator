#include "gpt.h"

#include "dsp56kEmu/logging.h"

namespace mc68k
{
	void Gpt::write8(PeriphAddress _addr, uint8_t _val)
	{
		PeripheralBase::write8(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::DdrGp:
			m_portGP.setDirection(_val);
			return;
		case PeriphAddress::PortGp:
			m_portGP.writeTX(_val);
			return;
		}

//		LOG("write8 addr=" << HEXN(_addr, 8) << ", val=" << HEX(static_cast<int>(_val),2));
	}

	uint8_t Gpt::read8(PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::PortGp:
			return m_portGP.read();
		}
		return PeripheralBase::read8(_addr);
	}

	void Gpt::write16(PeriphAddress _addr, uint16_t _val)
	{
		PeripheralBase::write16(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::DdrGp:
			m_portGP.setDirection(_val & 0xff);
			m_portGP.writeTX(_val>>8);
			break;
		}
	}

	uint16_t Gpt::read16(PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::DdrGp:
			{
				const uint16_t dir = m_portGP.getDirection();
				const uint16_t data = m_portGP.read();
				return dir << 8 | data;
			}
			break;
		}
		return PeripheralBase::read16(_addr);
	}
}

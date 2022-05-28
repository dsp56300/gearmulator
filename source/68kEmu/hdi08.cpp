#include "hdi08.h"

#include "dsp56kEmu/logging.h"

namespace mc68k
{
	uint8_t Hdi08::read8(PeriphAddress _addr)
	{
		auto r = PeripheralBase::read8(_addr);

		switch (_addr)
		{
		case PeriphAddress::HdiISR:
			// transmit data is always empty, we want new data
			r |= Txde;
			return r;
		}
		return r;
	}

	uint16_t Hdi08::read16(PeriphAddress _addr)
	{
		return PeripheralBase::read16(_addr);
	}

	void Hdi08::write8(PeriphAddress _addr, uint8_t _val)
	{
		PeripheralBase::write8(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::HdiCVR:
			if(_val & Hc)
			{
				LOG("HDI08 Host Vector Interrupt Request, interrupt vector = " << HEXN((_val & Hv) * 2, 2));
			}
			break;
		}
	}

	void Hdi08::write16(PeriphAddress _addr, uint16_t _val)
	{
		PeripheralBase::write16(_addr, _val);
	}
}

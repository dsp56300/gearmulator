#include "peripheralBase.h"

#include "mc68k.h"

namespace mc68k
{
	void periphBaseWriteW(uint8_t* _buffer, const uint32_t _addr, const uint16_t _val)
	{
		Mc68k::writeW(_buffer, _addr, _val);
	}

	uint16_t periphBaseReadW(const uint8_t* _buffer, const uint32_t _addr)
	{
		return Mc68k::readW(_buffer, _addr);
	}
}

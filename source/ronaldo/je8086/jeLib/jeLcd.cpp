#include "jeLcd.h"

namespace jeLib
{
	Lcd::Lcd()
	= default;

	void Lcd::write(const uint32_t _addr, const uint8_t _val)
	{
		exec((_addr & 1) != 0, false, _val);
	}

	uint8_t Lcd::read(uint32_t _addr)
	{
		return 0;
	}
}

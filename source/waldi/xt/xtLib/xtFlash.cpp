#include "xtFlash.h"

#include <cassert>

namespace xt
{
	Flash::Flash(uint8_t* _buffer, const size_t _size, const bool _useWriteEnable, const bool _bitreversedCmdAddr) : hwLib::Am29f(_buffer, _size, _useWriteEnable, _bitreversedCmdAddr)
	{
	}

	bool Flash::eraseSector(const uint32_t _addr) const
	{
		switch (_addr)
		{
		case 0x00000*2:
		case 0x04000*2:
		case 0x08000*2:
		case 0x0C000*2:
		case 0x10000*2:
		case 0x14000*2:
		case 0x18000*2:
		case 0x1C000*2:	return Am29f::eraseSector(_addr, 32);
		default:
			assert(false);
			return false;
		}
	}
}

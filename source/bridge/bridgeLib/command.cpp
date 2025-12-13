#include "command.h"

#include "baseLib/endian.h"

namespace bridgeLib
{
	static_assert(baseLib::hostEndian() == baseLib::Endian::Little, "big endian systems not supported");
}

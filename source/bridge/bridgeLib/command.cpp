#include "command.h"

namespace bridgeLib
{
	enum class HostEndian
	{
		Big,
		Little
	};

	constexpr HostEndian hostEndian()
	{
		constexpr uint32_t test32 = 0x01020304;
		constexpr uint8_t test8 = static_cast<const uint8_t&>(test32);

		static_assert(test8 == 0x01 || test8 == 0x04, "unable to determine endianess");

		return test8 == 0x01 ? HostEndian::Big : HostEndian::Little;
	}

	static_assert(hostEndian() == HostEndian::Little, "big endian systems not supported");
}

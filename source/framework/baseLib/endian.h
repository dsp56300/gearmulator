#pragma once

#include <cstdint>

namespace baseLib
{
	enum class Endian : uint8_t
	{
		Big,
		Little
	};

	constexpr Endian hostEndian()
	{
		constexpr uint32_t test32 = 0x01020304;
		constexpr uint8_t test8 = static_cast<const uint8_t&>(test32);

		static_assert(test8 == 0x01 || test8 == 0x04, "unable to determine endianess");

		return test8 == 0x01 ? Endian::Big : Endian::Little;
	}
}

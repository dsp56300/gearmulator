#pragma once

#include <cstdint>
#include <vector>

#include "jemiditypes.h"

namespace jeLib
{
	class State
	{
	public:
		using Dump = std::vector<uint8_t>;

		using Address = uint32_t;

		static constexpr Address InvalidAddress = 0xFFFFFFFF;

		static Address getAddress(const Dump& _dump);
		static AddressArea getAddressArea(const Dump& _dump);
		static AddressArea getAddressArea(Address _addr);
	};
}

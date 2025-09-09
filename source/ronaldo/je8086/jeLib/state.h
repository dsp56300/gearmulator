#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include <string>

#include "jemiditypes.h"

#include "jucePluginLib/patchdb/patchdbtypes.h"

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

		static uint32_t getBankNumber(Address _addr);
		static uint32_t getProgramNumber(Address _addr);
		static std::optional<std::string> getName(const Dump& _dump);
	};
}

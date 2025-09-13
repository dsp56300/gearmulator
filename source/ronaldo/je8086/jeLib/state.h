#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include <string>

#include "jemiditypes.h"
#include "common/storage.h"

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

		static bool is14BitData(Patch _param);

		static Dump createHeader(SysexByte _command, SysexByte _deviceId, const rLib::Storage::Address4& _address);
		static uint8_t calcChecksum(const Dump& _dump);
		static Dump& createFooter(Dump& _dump);

		static rLib::Storage::Address4 toAddress(uint32_t _addr);
		static UserPatchArea userPatchArea(uint32_t _index);
		static UserPerformanceArea userPerformanceArea(uint32_t _index);
		static rLib::Storage::Address4 toAddress(PerformanceData _performanceData, Patch _param);

		static Dump& addParameter(Dump& _dump, Patch _param, int _paramValue);
		static Dump createParameterChange(PerformanceData _performanceData, Patch _param, int _paramValue);
	};
}

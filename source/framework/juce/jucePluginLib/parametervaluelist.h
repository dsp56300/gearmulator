#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <limits>

#include "types.h"

namespace pluginLib
{
	struct ValueList
	{
		static constexpr uint32_t InvalidIndex = 0xffffffff;
		static constexpr ParamValue InvalidValue = std::numeric_limits<int>::min();

		uint32_t textToValue(const std::string& _string) const;
		const std::string& valueToText(const uint32_t _value) const;
		ParamValue orderToValue(uint32_t _orderIndex) const;
		const std::string& orderToText(uint32_t _orderIndex) const;

		std::vector<std::string> texts;
		std::map<std::string, uint32_t> textToValueMap;
		std::vector<ParamValue> order;
	};
}

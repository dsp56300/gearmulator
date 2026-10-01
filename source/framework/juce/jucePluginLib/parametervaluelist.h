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
		std::string valueToText(const uint32_t _value) const;
		ParamValue orderToValue(uint32_t _orderIndex) const;
		std::string orderToText(uint32_t _orderIndex) const;

		std::vector<std::string> texts;
		std::map<std::string, uint32_t> textToValueMap;
		std::vector<ParamValue> order;

		// A list synthesised from a format makes its texts when asked, it holds none: one per value of a 14 bit
		// parameter, copied into each description, cost megabytes per parameter. Index i is the value i + first,
		// shown as value * scale + offset
		struct Format
		{
			std::string format;
			double scale = 1.0;
			double offset = 0.0;
			int first = 0;
			uint32_t count = 0;
		};
		Format synthesized;
	};
}

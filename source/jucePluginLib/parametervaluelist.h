#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pluginLib
{
	struct ValueList
	{
		static constexpr uint32_t InvalidIndex = 0xffffffff;

		uint32_t textToValue(const std::string& _string) const;
		const std::string& valueToText(const uint32_t _value) const;
		uint32_t orderToValue(uint32_t _orderIndex) const;
		const std::string& orderToText(uint32_t _orderIndex) const;

		std::vector<std::string> texts;
		std::map<std::string, uint32_t> textToValueMap;
		std::vector<uint32_t> order;
	};
}

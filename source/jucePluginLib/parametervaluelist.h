#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pluginLib
{
	struct ValueList
	{
		uint32_t textToValue(const std::string& _string) const;
		const std::string& valueToText(const uint32_t _value) const;

		std::vector<std::string> texts;
		std::map<std::string, uint32_t> textToValueMap;
	};
}

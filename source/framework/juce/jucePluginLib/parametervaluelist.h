#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <limits>

#include "types.h"

namespace pluginLib
{
	// The texts of a parameter's values made by the plugin's code, for a description's "toText": {"formatter": ...}
	// (ParameterDescriptions::FormatterFactory)
	class ValueFormatter
	{
	public:
		virtual ~ValueFormatter() = default;

		// the text of a value of the parameter's range
		virtual std::string toText(int _value) const = 0;

		// the value of the parameter's range that a text stands for
		virtual int toValue(const std::string& _text) const = 0;
	};

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

		// a synthesised list whose texts the plugin's code makes instead of the format
		std::shared_ptr<const ValueFormatter> formatter;
	};
}

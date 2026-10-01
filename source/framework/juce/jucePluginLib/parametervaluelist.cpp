#include "parametervaluelist.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "juce_core/juce_core.h"

namespace pluginLib
{
	namespace
	{
		std::string g_empty;

		// Detect whether a printf-style format string expects an integer (%d/%i/%u/%x/%X/%o)
		// or a floating-point value, to pick the right snprintf overload
		bool formatExpectsInt(const std::string& _format)
		{
			bool inSpec = false;
			for (size_t i = 0; i < _format.size(); ++i)
			{
				const char c = _format[i];
				if (!inSpec)
				{
					if (c == '%')
					{
						if (i + 1 < _format.size() && _format[i + 1] == '%')
						{
							++i;
							continue;
						}
						inSpec = true;
					}
					continue;
				}
				if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X' || c == 'o')
					return true;
				if (c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G' || c == 'a' || c == 'A')
					return false;
			}
			return false;
		}

		std::string formatValue(const std::string& _format, double _value)
		{
			char buffer[64];
			if (formatExpectsInt(_format))
				std::snprintf(buffer, sizeof(buffer), _format.c_str(), juce::roundToInt(_value));
			else
				std::snprintf(buffer, sizeof(buffer), _format.c_str(), _value);
			return buffer;
		}
	}

	uint32_t ValueList::textToValue(const std::string& _string) const
	{
		if (synthesized.count)
		{
			// the number at the start of the text, back to the nearest value
			char* end = nullptr;
			const auto shown = std::strtod(_string.c_str(), &end);
			if (end == _string.c_str() || synthesized.scale == 0.0)
				return 0;
			const auto index = std::lround((shown - synthesized.offset) / synthesized.scale) - synthesized.first;
			return static_cast<uint32_t>(std::clamp<long>(index, 0, static_cast<long>(synthesized.count) - 1));
		}

		const auto it = textToValueMap.find(_string);
		if (it != textToValueMap.end())
			return it->second;
		return 0;
	}

	std::string ValueList::valueToText(const uint32_t _value) const
	{
		if (synthesized.count)
		{
			const auto index = std::min(_value, synthesized.count - 1);
			const auto value = static_cast<double>(static_cast<int>(index) + synthesized.first);
			return formatValue(synthesized.format, value * synthesized.scale + synthesized.offset);
		}

		if (_value >= texts.size())
			return texts.back();
		return texts[_value];
	}

	ParamValue ValueList::orderToValue(const uint32_t _orderIndex) const
	{
		if(_orderIndex >= order.size())
			return InvalidValue;
		return order[_orderIndex];
	}

	std::string ValueList::orderToText(const uint32_t _orderIndex) const
	{
		const auto value = orderToValue(_orderIndex);
		if(value == InvalidValue)
			return g_empty;
		return valueToText(value);
	}
}

#include "parametervaluelist.h"

namespace pluginLib
{
	namespace
	{
		std::string g_empty;
	}

	uint32_t ValueList::textToValue(const std::string& _string) const
	{
		const auto it = textToValueMap.find(_string);
		if (it != textToValueMap.end())
			return it->second;
		return 0;
	}

	const std::string& ValueList::valueToText(const uint32_t _value) const
	{
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

	const std::string& ValueList::orderToText(const uint32_t _orderIndex) const
	{
		const auto value = orderToValue(_orderIndex);
		if(value == InvalidValue)
			return g_empty;
		return valueToText(value);
	}
}

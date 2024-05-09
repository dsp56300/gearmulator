#include "parametervaluelist.h"

namespace pluginLib
{
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
}

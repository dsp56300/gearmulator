#pragma once

#include <set>
#include <vector>

namespace pluginLib
{
	struct ParameterLink
	{
		enum class LinkMode
		{
			Absolute,
			Relative
		};

		bool isValid() const
		{
			return source != dest;
		}

		bool hasConditions() const
		{
			return !conditionValues.empty();
		}

		uint32_t source = 0;
		uint32_t dest = 0;

		uint32_t conditionParameter;
		std::set<uint8_t> conditionValues;

		LinkMode mode = LinkMode::Absolute;
	};
}

#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <limits>

#include "parametervaluelist.h"

#include "juce_core/juce_core.h"

namespace pluginLib
{
	enum class ParameterClass
	{
		None             = 0x00,
        Global           = 0x01,
        MultiOrSingle    = 0x02,
        NonPartSensitive = 0x04,
	};

	struct Description
	{
		static constexpr int NoDefaultValue = std::numeric_limits<int>::max();

		uint8_t page;
		uint8_t index;
		int classFlags;
		std::string name;
		std::string displayName;
		juce::Range<int> range;
		int defaultValue = NoDefaultValue;
		ValueList valueList;
		bool isPublic;
		bool isDiscrete;
		bool isBool;
		bool isBipolar;
		int step = 0;
		std::string toText;
		std::string softKnobTargetSelect;
		std::string softKnobTargetList;

		bool isNonPartSensitive() const;
		bool isSoftKnob() const;
	};
}

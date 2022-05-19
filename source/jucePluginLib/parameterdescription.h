#pragma once

#include <cstdint>
#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

namespace pluginLib
{
	struct ValueList
	{
		std::vector<std::string> texts;
		std::map<std::string, uint32_t> textToValueMap;

		uint32_t textToValue(const std::string& _string) const
		{
			const auto it = textToValueMap.find(_string);
			if (it != textToValueMap.end())
				return it->second;
			return 0;
		}

		const std::string& valueToText(const uint32_t _value) const
		{
			if (_value >= texts.size())
				return texts.back();
			return texts[_value];
		}
	};
	enum class ParameterClass
	{
		None             = 0x00,
        Global           = 0x01,
        MultiOrSingle    = 0x02,
        NonPartSensitive = 0x04,
	};
	struct Description
	{
		uint8_t page;
		uint8_t index;
		int classFlags;
		std::string name;
		juce::Range<int> range;
		ValueList valueList;
		bool isPublic;
		bool isDiscrete;
		bool isBool;
		bool isBipolar;
		std::string toText;

		bool isNonPartSensitive() const;
	};
}

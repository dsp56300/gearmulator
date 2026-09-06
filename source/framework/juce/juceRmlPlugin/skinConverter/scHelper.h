#pragma once

#include <string>

namespace juce
{
	class Colour;
}

namespace rmlPlugin::skinConverter
{
	namespace helper
	{
		std::string toRmlColorString(const juce::Colour& _color);
	}
}

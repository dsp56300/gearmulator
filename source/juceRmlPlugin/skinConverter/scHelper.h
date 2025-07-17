#pragma once

#include "RmlUi/Core/Types.h"

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

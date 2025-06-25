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
		Rml::Colourb toRmlColor(const juce::Colour& _color);
		std::string toRmlColorString(const juce::Colour& _color);
	}
}

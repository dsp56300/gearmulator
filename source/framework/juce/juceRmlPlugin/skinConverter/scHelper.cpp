#include "scHelper.h"

#include "juce_graphics/juce_graphics.h"

namespace rmlPlugin::skinConverter
{
	std::string helper::toRmlColorString(const juce::Colour& _color)
	{
		char temp[64];
		if (_color.getAlpha() != 255)
			(void)snprintf(temp, sizeof(temp), "#%02x%02x%02x%02x", _color.getRed(), _color.getGreen(), _color.getBlue(), _color.getAlpha());
		else
			(void)snprintf(temp, sizeof(temp), "#%02x%02x%02x", _color.getRed(), _color.getGreen(), _color.getBlue());
		return temp;
	}
}

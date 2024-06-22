#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace xtJucePlugin
{
	struct WaveEditorStyle
	{
		juce::Colour colGraphLine = juce::Colour(0xffffffff);
		juce::Colour colGraphLineHighlighted = juce::Colour(0xffffaa00);
		float graphLineThickness = 3.0f;
		float graphPointSize = 10.0f;
		float graphPointSizeHighlighted = 30.0f;
	};
}

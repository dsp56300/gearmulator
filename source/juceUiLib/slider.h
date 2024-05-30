#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace genericUI
{
	class Slider : public juce::Slider
	{
		void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
	};
}

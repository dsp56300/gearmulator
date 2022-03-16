#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class TextButtonStyle : public UiObjectStyle
	{
		void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
	public:
		void apply(juce::Button& _target) const;
	};
}

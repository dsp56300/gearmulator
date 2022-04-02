#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class TextButtonStyle : public UiObjectStyle
	{
	public:
		explicit TextButtonStyle(Editor& _editor) : UiObjectStyle(_editor) {}

	private:
		void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
	public:
		void apply(juce::Button& _target) const;
	};
}

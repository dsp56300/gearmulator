#include "textbuttonStyle.h"

namespace genericUI
{
	void TextButtonStyle::drawButtonBackground(juce::Graphics& _graphics, juce::Button& _button, const juce::Colour& backgroundColour, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
	{
	}

	juce::Font TextButtonStyle::getTextButtonFont(juce::TextButton& _textButton, int buttonHeight)
	{
		if (const auto f = getFont())
			return *f;
		return UiObjectStyle::getTextButtonFont(_textButton, buttonHeight);
	}

	void TextButtonStyle::apply(juce::Button& _target) const
	{
		_target.setColour(juce::TextButton::ColourIds::textColourOffId, m_color);
		_target.setColour(juce::TextButton::ColourIds::textColourOnId, m_color);
		_target.setButtonText(m_text);
	}
}

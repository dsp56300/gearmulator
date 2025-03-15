#include "settingsStyles.h"

namespace jucePluginEditorLib::settings
{
	Style& getStyle()
	{
		static Style s;
		return s;
	}

	Style& getStyleHeadline1()
	{
		static StyleHeadline1 s;
		return s;
	}

	Style& getStyleHeadline2()
	{
		static StyleHeadline2 s;
		return s;
	}

	juce::Font Style::getTextButtonFont(juce::TextButton& _textButton, int _buttonHeight)
	{
		return {20};
	}

	juce::Font Style::getLabelFont(juce::Label& _label)
	{
		return {20};
	}

	void Style::apply(juce::Component* _component)
	{
		_component->setLookAndFeel(this);
	}

	juce::Font StyleHeadline1::getLabelFont(juce::Label& _label)
	{
		return {45};
	}

	void StyleHeadline1::drawLabel(juce::Graphics& _graphics, juce::Label& _label)
	{
		_label.setColour(juce::Label::backgroundColourId, juce::Colour(0x11000000));

		Style::drawLabel(_graphics, _label);
	}

	juce::Font StyleHeadline2::getLabelFont(juce::Label& _label)
	{
		return {30};
	}
}

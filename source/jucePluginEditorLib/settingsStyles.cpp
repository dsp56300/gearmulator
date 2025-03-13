#include "settingsStyles.h"

namespace jucePluginEditorLib::settings
{
	Style& getStyle()
	{
		static Style g_style;
		return g_style;
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
}

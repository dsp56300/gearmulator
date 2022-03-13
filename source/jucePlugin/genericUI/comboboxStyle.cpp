#include "comboboxStyle.h"

namespace genericUI
{
	juce::Font ComboboxStyle::getComboBoxFont(juce::ComboBox& _comboBox)
	{
		auto font = UiObjectStyle::getComboBoxFont(_comboBox);
		applyFontProperties(font);
		return font;
	}

	juce::Font ComboboxStyle::getLabelFont(juce::Label& _label)
	{
		auto font = UiObjectStyle::getLabelFont(_label);
		applyFontProperties(font);
		return font;
	}

	juce::Font ComboboxStyle::getPopupMenuFont()
	{
		auto font = UiObjectStyle::getPopupMenuFont();
		applyFontProperties(font);
		return font;
	}

	void ComboboxStyle::drawComboBox(juce::Graphics& _graphics, int width, int height, bool isButtonDown, int buttonX,
	                                 int buttonY, int buttonW, int buttonH, juce::ComboBox& _comboBox)
	{
	}
}

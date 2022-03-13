#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class ComboboxStyle : public UiObjectStyle
	{
		juce::Font getComboBoxFont(juce::ComboBox&) override;
		juce::Font getLabelFont(juce::Label&) override;
		juce::Font getPopupMenuFont() override;
		void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
	};
}
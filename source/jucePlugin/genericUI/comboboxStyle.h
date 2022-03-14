#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class ComboboxStyle : public UiObjectStyle
	{
		void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
	};
}
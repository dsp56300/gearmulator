#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class ComboboxStyle : public UiObjectStyle
	{
	public:
		explicit ComboboxStyle(Editor& _editor);

		void apply(juce::ComboBox& _target) const;

	private:
		void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
		void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
	};
}
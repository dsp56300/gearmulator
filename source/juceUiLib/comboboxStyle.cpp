#include "comboboxStyle.h"

namespace genericUI
{
	void ComboboxStyle::apply(juce::ComboBox& _target) const
	{
		_target.setColour(juce::ComboBox::ColourIds::textColourId, m_color);
	}

	void ComboboxStyle::drawComboBox(juce::Graphics& _graphics, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& _comboBox)
	{
	}
}

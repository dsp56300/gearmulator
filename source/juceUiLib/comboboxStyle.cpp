#include "comboboxStyle.h"

namespace genericUI
{
	ComboboxStyle::ComboboxStyle(Editor& _editor): UiObjectStyle(_editor)
	{
		m_offsetL = 1;
		m_offsetT = 1;
		m_offsetR = -30;
		m_offsetB = -2;
	}

	void ComboboxStyle::apply(juce::ComboBox& _target) const
	{
		_target.setColour(juce::ComboBox::ColourIds::textColourId, m_color);
	}

	void ComboboxStyle::drawComboBox(juce::Graphics& _graphics, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& _comboBox)
	{
	}

	void ComboboxStyle::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
	{
	    label.setBounds (m_offsetL, m_offsetT,
	                     box.getWidth() + m_offsetR,
	                     box.getHeight() + m_offsetB);

	    label.setFont (getComboBoxFont (box));
	}
}

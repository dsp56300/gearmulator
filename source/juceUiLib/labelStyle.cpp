#include "labelStyle.h"

namespace genericUI
{
	void LabelStyle::apply(juce::Label& _target) const
	{
		if(m_bgColor.getARGB())
			_target.setColour(juce::Label::ColourIds::backgroundColourId, m_bgColor);
		_target.setColour(juce::Label::ColourIds::textColourId, m_color);
		_target.setText(m_text, juce::dontSendNotification);
		_target.setJustificationType(m_align);
	}
}

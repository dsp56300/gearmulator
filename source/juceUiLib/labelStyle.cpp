#include "labelStyle.h"

namespace genericUI
{
	void LabelStyle::apply(juce::Label& _target) const
	{
		_target.setColour(juce::Label::ColourIds::textColourId, m_color);
		_target.setText(m_text, juce::dontSendNotification);
		_target.setJustificationType(m_align);
	}
}

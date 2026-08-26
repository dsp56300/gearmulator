#include "labelStyle.h"

namespace genericUI
{
	juce::Font LabelStyle::getPopupMenuFont()
	{
		return LookAndFeel_V4::getPopupMenuFont();  // NOLINT(bugprone-parent-virtual-call)
	}

	void LabelStyle::apply(juce::Label& _target) const
	{
		if(m_bgColor.getARGB())
			_target.setColour(juce::Label::ColourIds::backgroundColourId, m_bgColor);
		_target.setColour(juce::Label::ColourIds::textColourId, m_color);
		if(!m_text.empty())
			_target.setText(m_text, juce::dontSendNotification);
		_target.setJustificationType(m_align);
	}
}

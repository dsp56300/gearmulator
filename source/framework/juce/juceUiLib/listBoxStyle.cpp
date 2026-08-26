#include "listBoxStyle.h"

namespace genericUI
{
	void ListBoxStyle::apply(juce::ListBox& _target) const
	{
		_target.setColour(juce::ListBox::ColourIds::backgroundColourId, m_bgColor);
		_target.setColour(juce::ListBox::ColourIds::outlineColourId, m_outlineColor);
		_target.setColour(juce::ListBox::ColourIds::textColourId, m_color);
	}
}

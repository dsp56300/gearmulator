#include "scrollbarStyle.h"

namespace genericUI
{
	void ScrollBarStyle::apply(juce::ScrollBar& _target) const
	{
		if(m_bgColor.getARGB())
		{
			_target.setColour(juce::ScrollBar::ColourIds::backgroundColourId, m_bgColor);
		}
		if(m_color.getARGB())
		{
			_target.setColour(juce::ScrollBar::ColourIds::thumbColourId, m_color);
			_target.setColour(juce::ScrollBar::ColourIds::trackColourId, m_color);
		}
	}
}

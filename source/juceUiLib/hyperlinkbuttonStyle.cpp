#include "hyperlinkbuttonStyle.h"

namespace genericUI
{
	void HyperlinkButtonStyle::apply(juce::HyperlinkButton& _target) const
	{
		_target.setColour(juce::HyperlinkButton::ColourIds::textColourId, m_color);
		const auto f = getFont();
		if (f)
			_target.setFont(*f, true);
		_target.setURL(juce::URL(juce::String(m_url)));

		TextButtonStyle::apply(_target);
	}
}

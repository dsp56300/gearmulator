#pragma once

#include "textbuttonStyle.h"

namespace juce
{
	class HyperlinkButton;
}

namespace genericUI
{
	class HyperlinkButtonStyle : public TextButtonStyle
	{
	public:
		void apply(juce::HyperlinkButton& _target) const;
	};
}

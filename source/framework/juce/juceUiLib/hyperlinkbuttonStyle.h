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
		explicit HyperlinkButtonStyle(Editor& _editor) : TextButtonStyle(_editor) {}

		void apply(juce::HyperlinkButton& _target) const;
	};
}

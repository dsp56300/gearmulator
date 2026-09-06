#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class LabelStyle : public UiObjectStyle
	{
	public:
		explicit LabelStyle(Editor& _editor) : UiObjectStyle(_editor) {}

		juce::Font getPopupMenuFont() override;

		void apply(juce::Label& _target) const;
	};
}

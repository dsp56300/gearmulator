#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class ScrollBarStyle : public UiObjectStyle
	{
	public:
		explicit ScrollBarStyle(Editor& _editor) : UiObjectStyle(_editor) {}

		void apply(juce::ScrollBar& _target) const;
	};
}

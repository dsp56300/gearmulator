#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class LabelStyle : public UiObjectStyle
	{
	public:
		explicit LabelStyle(Editor& _editor) : UiObjectStyle(_editor) {}

		void apply(juce::Label& _target) const;
	};
}

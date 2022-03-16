#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class LabelStyle : public UiObjectStyle
	{
	public:
		void apply(juce::Label& _target) const;
	};
}

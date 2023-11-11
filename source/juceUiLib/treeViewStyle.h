#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class TreeViewStyle : public UiObjectStyle
	{
	public:
		TreeViewStyle(Editor& _editor) : UiObjectStyle(_editor) {}

		void apply(juce::TreeView& _target) const;
	};
}

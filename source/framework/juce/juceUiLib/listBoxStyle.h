#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class ListBoxStyle : public UiObjectStyle
	{
	public:
		explicit ListBoxStyle(Editor& _editor) : UiObjectStyle(_editor) {}

	public:
		void apply(juce::ListBox& _target) const;
	};
}

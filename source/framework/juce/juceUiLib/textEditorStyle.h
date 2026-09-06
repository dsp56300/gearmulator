#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class TextEditorStyle : public UiObjectStyle
	{
	public:
		explicit TextEditorStyle(Editor& _editor) : UiObjectStyle(_editor) {}

		juce::Font getPopupMenuFont() override;
	public:
		void apply(juce::TextEditor& _target) const;
	};
}

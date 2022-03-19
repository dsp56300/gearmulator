#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class ButtonStyle : public UiObjectStyle
	{
	public:
		explicit ButtonStyle(Editor& _editor) : UiObjectStyle(_editor) {}

		void apply(Editor& _editor, const UiObject& _object) override;
		void apply(juce::DrawableButton& _button) const;

	private:
		bool m_isToggle = false;
		int m_radioGroupId = 0;

		juce::Drawable* m_normalImage = nullptr;
		juce::Drawable* m_overImage = nullptr;
		juce::Drawable* m_downImage = nullptr;
		juce::Drawable* m_disabledImage = nullptr;
		juce::Drawable* m_normalImageOn = nullptr;
		juce::Drawable* m_overImageOn = nullptr;
		juce::Drawable* m_downImageOn = nullptr;
		juce::Drawable* m_disabledImageOn = nullptr;

		std::vector<std::unique_ptr<juce::Drawable>> m_createdDrawables;
	};
}

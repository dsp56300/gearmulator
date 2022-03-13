#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class ButtonStyle : public UiObjectStyle
	{
	public:
		void apply(Editor& _editor, const UiObject& _object) override;
		void setImages(juce::DrawableButton& _button) const;

	private:
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

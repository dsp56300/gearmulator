#pragma once

#include "juceUiLib/slider.h"

namespace genericUI
{
	class UiObject;
}

namespace n2xJucePlugin
{
	class Editor;

	class Slider : public genericUI::Slider
	{
	public:
		Slider(Editor& _editor);

		void modifierKeysChanged(const juce::ModifierKeys& _modifiers) override;

	private:
		Editor& m_editor;
	};
}

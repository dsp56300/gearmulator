#include "n2xSlider.h"

#include "n2xEditor.h"
#include "n2xVmMap.h"

namespace n2xJucePlugin
{
	Slider::Slider(Editor& _editor) : genericUI::Slider(), m_editor(_editor)
	{
	}

	void Slider::modifierKeysChanged(const juce::ModifierKeys& _modifiers)
	{
		m_editor.getVmMap().setEnabled(_modifiers.isShiftDown());
	}
}

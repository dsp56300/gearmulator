#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace jucePluginEditorLib
{
	class FocusedParameterTooltip
	{
	public:
		FocusedParameterTooltip(juce::Label* _label);

		bool isValid() const { return m_label != nullptr; }
		void setVisible(bool _visible) const;
		void initialize(juce::Component* _component, const juce::String& _value) const;

	private:
		juce::Label* m_label = nullptr;
	};
}

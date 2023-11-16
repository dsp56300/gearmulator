#pragma once

namespace juce
{
	class String;
	class Component;
	class Label;
}

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

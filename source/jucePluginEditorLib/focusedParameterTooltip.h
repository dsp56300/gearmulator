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
		FocusedParameterTooltip(juce::Label* _label, const juce::Component& _bounds);

		bool isValid() const { return m_label != nullptr; }
		void setVisible(bool _visible) const;
		void initialize(juce::Component* _component, const juce::String& _value) const;

	private:
		juce::Label* m_label = nullptr;
		int m_defaultWidth;
		const juce::Component& m_bounds;
	};
}

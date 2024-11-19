#pragma once

#include "focusedParameterTooltip.h"

#include "juce_events/juce_events.h"

#include "jucePluginLib/parameterlistener.h"

namespace juce
{
	class MouseEvent;
}

namespace genericUI
{
	class Editor;
}

namespace pluginLib
{
	class Parameter;
	class Controller;
	class ParameterBinding;
}

namespace jucePluginEditorLib
{
	class FocusedParameter : juce::Timer
	{
	public:
		enum class Priority
		{
			None,
			Low,
			Medium,
			High
		};

		FocusedParameter(const pluginLib::Controller& _controller, const pluginLib::ParameterBinding& _parameterBinding, const genericUI::Editor& _editor);
		~FocusedParameter() override;

		void onMouseEnter(const juce::MouseEvent& _event);

		void updateByComponent(juce::Component* _comp);

		virtual void updateParameter(const std::string& _name, const std::string& _value);

	private:
		void timerCallback() override;
		void updateControlLabel(juce::Component* _component, Priority _prio);
		void updateControlLabel(juce::Component* _component, const pluginLib::Parameter* _param);
		void updateControlLabel(juce::Component* _component, const pluginLib::Parameter* _param, Priority _priority);

		const pluginLib::Parameter* getParameterFromComponent(juce::Component* _component) const;

		const pluginLib::Parameter* resolveSoftKnob(const pluginLib::Parameter* _sourceParam) const;

		const pluginLib::ParameterBinding& m_parameterBinding;
		const pluginLib::Controller& m_controller;

		juce::Label* m_focusedParameterName = nullptr;
		juce::Label* m_focusedParameterValue = nullptr;

		std::unique_ptr<FocusedParameterTooltip> m_tooltip;

		std::map<pluginLib::Parameter*, pluginLib::ParameterListener> m_boundParameters;
		Priority m_currentPriority = Priority::None;
	};
}

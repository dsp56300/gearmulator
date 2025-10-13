#pragma once

#include "focusedParameterTooltip.h"

#include "juce_events/juce_events.h"

#include "jucePluginLib/parameterlistener.h"

#include "juceRmlPlugin/rmlPlugin.h"

#include "juceRmlUi/rmlEventListener.h"

namespace Rml
{
	class Element;
}

namespace pluginLib
{
	class Parameter;
	class Controller;
}

namespace jucePluginEditorLib
{
	class Editor;

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

		FocusedParameter(const pluginLib::Controller& _controller, const Editor& _editor);
		~FocusedParameter() override;

		void updateByElement(const Rml::Element* _element);

		virtual void updateParameter(const std::string& _name, const std::string& _value);

	private:
		void timerCallback() override;

		void updateControlLabel(const Rml::Element* _elem, Priority _prio);
		void updateControlLabel(const Rml::Element* _element, const pluginLib::Parameter* _param);
		void updateControlLabel(const Rml::Element* _element, const pluginLib::Parameter* _param, Priority _priority);

		const pluginLib::Parameter* getParameterFromElement(const Rml::Element* _element) const;

		const pluginLib::Parameter* resolveSoftKnob(const pluginLib::Parameter* _sourceParam) const;

		const rmlPlugin::RmlParameterBinding& m_parameterBinding;
		const pluginLib::Controller& m_controller;

		Rml::Element* m_focusedParameterName = nullptr;
		Rml::Element* m_focusedParameterValue = nullptr;

		std::unique_ptr<FocusedParameterTooltip> m_tooltip;

		std::map<pluginLib::Parameter*, pluginLib::ParameterListener> m_boundParameters;
		Priority m_currentPriority = Priority::None;
		juceRmlUi::ScopedListener m_mouseOver;

		const pluginLib::Parameter* m_currentParameter = nullptr;
	};
}

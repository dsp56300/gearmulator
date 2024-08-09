#pragma once

#include "jucePluginLib/parameterlistener.h"

#include <string>

namespace juce
{
	class Button;
}

namespace n2xJucePlugin
{
	class Editor;

	class ParameterDrivenLed
	{
	public:
		explicit ParameterDrivenLed(Editor& _editor, const std::string& _component, const std::string& _parameter);
		virtual ~ParameterDrivenLed() = default;

	protected:
		virtual bool updateToggleState(const pluginLib::Parameter* _parameter) const = 0;
		virtual void onClick(pluginLib::Parameter* _targetParameter, bool _toggleState) {}

		void bind();
		void disableClick() const;

	private:
		void updateStateFromParameter(const pluginLib::Parameter* _parameter) const;

		Editor& m_editor;
		const std::string m_parameterName;
		juce::Button* m_led;

		pluginLib::ParameterListener m_onParamChanged;
		pluginLib::EventListener<uint8_t> m_onCurrentPartChanged;
		pluginLib::Parameter* m_param = nullptr;
	};
}

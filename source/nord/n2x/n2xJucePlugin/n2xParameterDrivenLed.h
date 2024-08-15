#pragma once

#include "jucePluginLib/parameterlistener.h"

#include <string>
#include <cstdint>

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
		static constexpr uint8_t CurrentPart = 0xff;

		explicit ParameterDrivenLed(Editor& _editor, const std::string& _component, std::string _parameter, uint8_t _part = CurrentPart);
		virtual ~ParameterDrivenLed() = default;

	protected:
		virtual void updateState(juce::Button& _target, const pluginLib::Parameter* _source) const;
		virtual bool updateToggleState(const pluginLib::Parameter* _parameter) const = 0;
		virtual void onClick(pluginLib::Parameter* _targetParameter, bool _toggleState) {}

		void bind();
		void disableClick() const;

	private:
		void updateStateFromParameter(const pluginLib::Parameter* _parameter) const;

		Editor& m_editor;
		const std::string m_parameterName;
		juce::Button* m_led;
		const uint8_t m_part;

		pluginLib::ParameterListener m_onParamChanged;
		pluginLib::EventListener<uint8_t> m_onCurrentPartChanged;
		pluginLib::Parameter* m_param = nullptr;
	};
}

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "jucePluginLib/parameterlistener.h"

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	class Editor;

	// A button of the skin that is checked while a parameter is in a certain state, an LED that shows a mode for example.
	// The parameter is the one of a fixed part, or of the current part, then the LED follows part changes. A click calls
	// onClick with the parameter and the checked state of the button, without onClick the LED ignores the mouse.
	// onUpdate styles the element further after each change
	class ParameterDrivenLed
	{
	public:
		static constexpr uint8_t CurrentPart = 0xff;

		using IsOn = std::function<bool(const pluginLib::Parameter&)>;
		using OnClick = std::function<void(pluginLib::Parameter&, bool _checked)>;
		using OnUpdate = std::function<void(Rml::Element&, const pluginLib::Parameter&)>;

		ParameterDrivenLed(Editor& _editor, const std::string& _elementId, std::string _parameter, uint8_t _part, IsOn _isOn, OnClick _onClick = {}, OnUpdate _onUpdate = {});

	private:
		void bind();
		void update() const;

		Editor& m_editor;
		const std::string m_parameterName;
		const uint8_t m_part;
		Rml::Element* const m_led;
		const IsOn m_isOn;
		const OnClick m_onClick;
		const OnUpdate m_onUpdate;

		pluginLib::Parameter* m_param = nullptr;
		pluginLib::ParameterListener m_onParamChanged;
		baseLib::EventListener<uint8_t> m_onCurrentPartChanged;
	};
}

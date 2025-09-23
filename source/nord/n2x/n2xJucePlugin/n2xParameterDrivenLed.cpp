#include "n2xParameterDrivenLed.h"

#include "n2xController.h"
#include "n2xEditor.h"

#include "juceRmlUi/rmlElemButton.h"

namespace n2xJucePlugin
{
	ParameterDrivenLed::ParameterDrivenLed(Editor& _editor, const std::string& _component, std::string _parameter, uint8_t _part/* = CurrentPart*/)
		: m_editor(_editor)
		, m_parameterName(std::move(_parameter))
		, m_led(_editor.findChild(_component))
		, m_part(_part)
	{
		auto& c = _editor.getN2xController();

		m_onCurrentPartChanged.set(c.onCurrentPartChanged, [this](const uint8_t&)
		{
			bind();
		});

		juceRmlUi::EventListener::AddClick(m_led, [this]
		{
			if(!m_param)
				return;
			onClick(m_param, juceRmlUi::ElemButton::isChecked(m_led));
		});
	}

	void ParameterDrivenLed::updateState(Rml::Element& _target, const pluginLib::Parameter* _source) const
	{
		juceRmlUi::ElemButton::setChecked(&_target, updateToggleState(_source));
	}

	void ParameterDrivenLed::bind()
	{
		const auto& c = m_editor.getN2xController();

		m_param = c.getParameter(m_parameterName, m_part == CurrentPart ? c.getCurrentPart() : m_part);

		m_onParamChanged.set(m_param, [this](const pluginLib::Parameter* _parameter)
		{
			updateStateFromParameter(_parameter);
		});

		updateStateFromParameter(m_param);
	}

	void ParameterDrivenLed::disableClick() const
	{
		m_led->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
	}

	void ParameterDrivenLed::updateStateFromParameter(const pluginLib::Parameter* _parameter) const
	{
		updateState(*m_led, _parameter);
	}
}

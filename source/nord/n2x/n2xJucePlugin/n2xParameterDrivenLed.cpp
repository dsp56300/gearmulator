#include "n2xParameterDrivenLed.h"

#include "n2xController.h"
#include "n2xEditor.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace n2xJucePlugin
{
	ParameterDrivenLed::ParameterDrivenLed(Editor& _editor, const std::string& _component, std::string _parameter, uint8_t _part/* = CurrentPart*/)
		: m_editor(_editor)
		, m_parameterName(std::move(_parameter))
		, m_led(_editor.findComponentT<juce::Button>(_component))
		, m_part(_part)
	{
		auto& c = _editor.getN2xController();

		m_onCurrentPartChanged.set(c.onCurrentPartChanged, [this](const uint8_t&)
		{
			bind();
		});

		m_led->onClick = [this]
		{
			if(!m_param)
				return;
			onClick(m_param, m_led->getToggleState());
		};
	}

	void ParameterDrivenLed::updateState(juce::Button& _target, const pluginLib::Parameter* _source) const
	{
		_target.setToggleState(updateToggleState(_source), juce::dontSendNotification);
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
		m_led->setInterceptsMouseClicks(false, false);
	}

	void ParameterDrivenLed::updateStateFromParameter(const pluginLib::Parameter* _parameter) const
	{
		updateState(*m_led, _parameter);
	}
}

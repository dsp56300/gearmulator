#include "n2xParameterDrivenLed.h"

#include "n2xController.h"
#include "n2xEditor.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace n2xJucePlugin
{
	ParameterDrivenLed::ParameterDrivenLed(Editor& _editor, const std::string& _component, const std::string& _parameter)
		: m_editor(_editor)
		, m_parameterName(_parameter)
		, m_led(_editor.findComponentT<juce::Button>(_component))
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

	void ParameterDrivenLed::bind()
	{
		const auto& c = m_editor.getN2xController();

		m_param = c.getParameter(m_parameterName, c.getCurrentPart());

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
		m_led->setToggleState(updateToggleState(_parameter), juce::dontSendNotification);
	}
}

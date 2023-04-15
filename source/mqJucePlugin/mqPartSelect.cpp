#include "mqPartSelect.h"

#include "mqController.h"
#include "mqEditor.h"

mqPartSelect::mqPartSelect(const mqJucePlugin::Editor& _editor, Controller& _controller, pluginLib::ParameterBinding& _parameterBinding)
	: m_controller(_controller)
	, m_parameterBinding(_parameterBinding)
{
	std::vector<juce::Button*> buttons;
	std::vector<juce::Button*> leds;

	_editor.findComponents(buttons, "partSelectButton", 16);
	_editor.findComponents(leds, "partSelectLED", 16);

	for(size_t i=0; i<m_parts.size(); ++i)
	{
		auto& part = m_parts[i];

		part.button = buttons[i];
		part.led = leds[i];

		auto index = static_cast<uint8_t>(i);

		part.button->onClick = [this,index]	{ selectPart(index); };
		part.led->onClick = [this, index]	{ selectPart(index); };
	}

	updateUiState();
}

void mqPartSelect::updateUiState() const
{
	const auto current = m_controller.getCurrentPart();

	for(size_t i=0; i<m_parts.size(); ++i)
	{
		const auto& part = m_parts[i];

		part.button->setToggleState(i == current, juce::dontSendNotification);
		part.led->setToggleState(i == current, juce::dontSendNotification);
	}
}

void mqPartSelect::selectPart(const uint8_t _index) const
{
	m_parameterBinding.setPart(_index);
	updateUiState();
}

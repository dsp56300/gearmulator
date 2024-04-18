#include "xtParts.h"

#include "xtController.h"
#include "xtEditor.h"

namespace xtJucePlugin
{
	Parts::Parts(Editor& _editor) : m_editor(_editor)
	{
		std::vector<PartButton*> buttons;
		std::vector<juce::Button*> leds;

		_editor.findComponents<PartButton>(buttons, "PartButtonSmall", 8);
		_editor.findComponents<juce::Button>(leds, "PartLedSmall", 8);

		for(size_t i=0; i<m_parts.size(); ++i)
		{
			auto& part = m_parts[i];
			part.m_button = buttons[i];
			part.m_led = leds[i];

			part.m_button->setPart(static_cast<uint8_t>(i));
		}

		updateUi();
	}

	bool Parts::selectPart(const uint8_t _part)
	{
		if(_part >= m_parts.size())
			return false;
		if(_part > 0 && !m_editor.getXtController().isMultiMode())
			return false;
		m_editor.setCurrentPart(_part);

		updateUi();

		return true;
	}

	void Parts::updateUi()
	{
		const auto currentPart = m_editor.getXtController().getCurrentPart();

		for(size_t i=0; i<m_parts.size(); ++i)
		{
			auto& part = m_parts[i];

			part.m_led->setToggleState(i == currentPart, juce::dontSendNotification);
			part.m_button->setToggleState(i == currentPart, juce::dontSendNotification);
		}
	}
}

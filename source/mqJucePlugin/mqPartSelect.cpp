#include "mqPartSelect.h"

#include "mqController.h"
#include "mqEditor.h"
#include "mqPartButton.h"
#include "juceRmlPlugin/rmlTabGroup.h"

#include "RmlUi/Core/ElementDocument.h"

namespace mqJucePlugin
{
	mqPartSelect::mqPartSelect(Editor& _editor, Controller& _controller)
		: m_editor(_editor)
		, m_controller(_controller)
	{
		std::vector<Rml::Element*> buttons;
		std::vector<Rml::Element*> leds;

		const auto* doc = _editor.getDocument();

		juceRmlUi::helper::findChildren(buttons, doc, "partSelectButton", 16);
		juceRmlUi::helper::findChildren(leds, doc, "partSelectLED", 16);

		for(size_t i=0; i<m_parts.size(); ++i)
		{
			auto& part = m_parts[i];

			part.button.reset(new mqPartButton(buttons[i], _editor));
			part.led = leds[i];

			auto index = static_cast<uint8_t>(i);

			part.button->initalize(static_cast<uint8_t>(i));

			juceRmlUi::EventListener::AddClick(part.led, [this, index]
			{
				selectPart(index);
			});
		}

		updateUiState();
	}

	mqPartSelect::~mqPartSelect()
	= default;

	void mqPartSelect::onPlayModeChanged() const
	{
		if(m_controller.getCurrentPart() > 0)
			selectPart(0);
		else
			updateUiState();
	}

	void mqPartSelect::updateUiState() const
	{
		const auto current = m_controller.isMultiMode() ? m_controller.getCurrentPart() : static_cast<uint8_t>(0);

		for(size_t i=0; i<m_parts.size(); ++i)
		{
			const auto& part = m_parts[i];

			part.button->setChecked(i == current);
			rmlPlugin::TabGroup::setChecked(part.led, i == current);

			if(i > 0)
			{
				part.button->setVisible(m_controller.isMultiMode());
				juceRmlUi::helper::setVisible(part.led, m_controller.isMultiMode());
				/*
				part.button->setEnabled(m_controller.isMultiMode());
				part.led->setEnabled(m_controller.isMultiMode());

				part.button->setAlpha(1.0f);
				part.led->setAlpha(1.0f);
				*/
			}
		}
	}

	void mqPartSelect::selectPart(const uint8_t _index) const
	{
		m_editor.setCurrentPart(_index);
		updateUiState();
	}
}

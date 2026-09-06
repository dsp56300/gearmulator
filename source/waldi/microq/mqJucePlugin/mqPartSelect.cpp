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
		std::vector<Rml::Element*> patchNames;

		const auto* doc = _editor.getDocument();

		juceRmlUi::helper::findChildren(buttons, doc, "partSelectButton", 16);
		juceRmlUi::helper::findChildren(leds, doc, "partSelectLED", 16);
		juceRmlUi::helper::findChildren(patchNames, doc, "patchname");

		for(size_t i=0; i<m_parts.size(); ++i)
		{
			auto& part = m_parts[i];

			part.button.reset(new mqPartButton(buttons[i], _editor));
			part.led = leds[i];
			part.patchName = i < patchNames.size() ? patchNames[i] : nullptr;

			auto index = static_cast<uint8_t>(i);

			if (part.patchName)
			{
				part.patchNameButton.reset(new mqPartButton(part.patchName, _editor));
				part.patchNameButton->initalize(index);
			}

			part.button->initalize(index);

			juceRmlUi::EventListener::AddClick(part.led, [this, index]
			{
				selectPart(index);
			});
		}

		updateUiState();

		if (!patchNames.empty())
		{
			m_onPatchNameChanged.set(m_controller.onPatchNameChanged, [this](const uint8_t& _part)
			{
				onPatchNameChanged(_part);
			});
		}
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

				if (part.patchName)
				{
					juceRmlUi::helper::setVisible(part.patchName, m_controller.isMultiMode());

					if (m_controller.isMultiMode())
						updatePatchName(static_cast<uint8_t>(i));
				}
			}
			else
			{
				updatePatchName(0);
			}
		}
	}

	void mqPartSelect::selectPart(const uint8_t _index) const
	{
		m_editor.setCurrentPart(_index);
		updateUiState();
	}

	void mqPartSelect::updatePatchName(uint8_t _part) const
	{
		if(_part >= m_parts.size())
			return;

		const auto& part = m_parts[_part];

		if(part.patchName)
			part.patchName->SetInnerRML(Rml::StringUtilities::EncodeRml(m_controller.getPatchName(_part)));
	}

	void mqPartSelect::onPatchNameChanged(const uint8_t _part) const
	{
		updatePatchName(_part);
	}
}

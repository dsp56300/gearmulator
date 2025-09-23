#include "xtParts.h"

#include "xtController.h"
#include "xtEditor.h"
#include "xtPartName.h"
#include "juceRmlUi/rmlElemButton.h"

namespace xtJucePlugin
{
	Parts::Parts(Editor& _editor) : m_editor(_editor)
	{
		std::vector<Rml::Element*> buttons;
		std::vector<Rml::Element*> names;
		std::vector<Rml::Element*> leds;

		_editor.findChildren(buttons, "PartButtonSmall", 8);
		_editor.findChildren(names, "PatchName", 8);
		_editor.findChildren(leds, "PartLedSmall", 8);

		for(size_t i=0; i<m_parts.size(); ++i)
		{
			auto& part = m_parts[i];
			part.m_button.reset(new PartButton(buttons[i], _editor));
			part.m_name.reset(new PartName(names[i], _editor));
			part.m_led = leds[i];

			part.m_button->initalize(static_cast<uint8_t>(i));
			part.m_name->initalize(static_cast<uint8_t>(i));
		}

		updateUi();
	}

	bool Parts::selectPart(const uint8_t _part) const
	{
		auto fail = [this]()
		{
			juce::MessageManager::callAsync([this]
			{
				updateUi();
			});
			return false;
		};

		if(_part >= m_parts.size())
			return fail();
		if(_part > 0 && !m_editor.getXtController().isMultiMode())
			return fail();
		m_editor.setCurrentPart(_part);

		updateUi();

		return true;
	}

	void Parts::updateUi() const
	{
		const auto currentPart = m_editor.getXtController().getCurrentPart();

		for(size_t i=0; i<m_parts.size(); ++i)
		{
			const auto& part = m_parts[i];

			juceRmlUi::ElemButton::setChecked(part.m_led, i == currentPart);
			part.m_button->setChecked(i == currentPart);
		}
	}
}

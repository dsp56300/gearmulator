#include "jePartSelect.h"

#include "jeController.h"
#include "jeEditor.h"

#include "juceRmlUi/rmlElemButton.h"

namespace jeJucePlugin
{
	PartSelect::PartSelect(Editor& _editor )
	: m_editor(_editor)
	, m_paramPanelSelect(_editor.geJeController().getParameter("PanelSelect", 0))
	{
		m_partButtons[0] = m_editor.findChild<juceRmlUi::ElemButton>("partUpper");
		m_partButtons[1] = m_editor.findChild<juceRmlUi::ElemButton>("partLower");

		m_partSelectListener.set(m_paramPanelSelect, [this](const pluginLib::Parameter*)
		{
			onPartSelectChanged();
		});

		for (size_t i=0; i<m_partButtons.size(); ++i)
		{
			juceRmlUi::EventListener::Add(m_partButtons[i], Rml::EventId::Click, [i, this](Rml::Event& _event)
			{
				onClick(_event, static_cast<int>(i));
			});
		}

		updateButtonStates();
	}

	void PartSelect::onPartSelectChanged() const
	{
		updateButtonStates();
	}

	void PartSelect::updateButtonStates() const
	{
		const int part = m_paramPanelSelect->getUnnormalizedValue() + 1;

		m_partButtons[0]->setChecked(part & 1);
		m_partButtons[1]->setChecked(part & 2);
	}

	void PartSelect::onClick(Rml::Event& _event, size_t _part) const
	{
		const auto current = m_paramPanelSelect->getUnnormalizedValue();

		const auto part = static_cast<int>(_part);

		if (current == part)
		{
			// set to both if the current part is selected already
			m_paramPanelSelect->setUnnormalizedValue(2, pluginLib::Parameter::Origin::Ui);
		}
		else
		{
			// otherwise set to the clicked part
			m_paramPanelSelect->setUnnormalizedValue(part, pluginLib::Parameter::Origin::Ui);
			m_editor.setCurrentPart(static_cast<uint8_t>(_part));
		}

		_event.StopPropagation();
	}
}

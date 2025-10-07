#include "jePartSelect.h"

#include "jeController.h"
#include "jeEditor.h"

#include "juceRmlUi/rmlElemButton.h"

namespace jeJucePlugin
{
	PartSelect::PartSelect(Editor& _editor )
	: m_editor(_editor)
	, m_paramPanelSelect(_editor.getJeController().getParameter("PanelSelect", 0))
	{
		m_editor.findChildren<juceRmlUi::ElemButton>(m_partButtons[0], "partUpper");
		m_editor.findChildren<juceRmlUi::ElemButton>(m_partButtons[1], "partLower");

		m_editor.findChildren(m_patchNames[0], "patchNameTextUpper");
		m_editor.findChildren(m_patchNames[1], "patchNameTextLower");

		m_partSelectListener.set(m_paramPanelSelect, [this](const pluginLib::Parameter*)
		{
			onPartSelectChanged();
		});

		for (size_t p=0; p<m_partButtons.size(); ++p)
		{
			for (size_t i=0; i<m_partButtons[p].size(); ++i)
			{
				juceRmlUi::EventListener::Add(m_partButtons[p][i], Rml::EventId::Click, [p, this](Rml::Event& _event)
				{
					onClick(_event, static_cast<int>(p));
				});
			}

			for (size_t i=0; i<m_patchNames[p].size(); ++i)
			{
				// TODO: inplace editor
			}
		}

		updateButtonStates();

		m_onPatchNameChanged.set(_editor.getJeController().evPatchNameChanged, [this](const PatchType& _patch, const std::string& _name)
		{
			onPatchNameChanged(_patch, _name);
		});
	}

	PartSelect::~PartSelect() = default;

	void PartSelect::onPartSelectChanged() const
	{
		updateButtonStates();
	}

	void PartSelect::updateButtonStates() const
	{
		const int part = m_paramPanelSelect->getUnnormalizedValue() + 1;

		for (auto& b : m_partButtons[0])
			b->setChecked(part & 1);

		for (auto& b : m_partButtons[1])
			b->setChecked(part & 2);
	}

	void PartSelect::onClick(Rml::Event& _event, const size_t _part) const
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

	void PartSelect::onPatchNameChanged(PatchType _patch, const std::string& _name) const
	{
		if (_patch == PatchType::Performance)
			return;

		const auto part = _patch == PatchType::PartUpper ? 0 : 1;
		for (auto& e : m_patchNames[part])
			e->SetInnerRML(Rml::StringUtilities::EncodeRml(_name));
	}
}

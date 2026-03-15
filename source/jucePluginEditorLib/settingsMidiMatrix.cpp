#include "settingsMidiMatrix.h"

#include "pluginProcessor.h"

#include "settingsMidi.h"

#include "juceRmlUi/rmlElemButton.h"

namespace jucePluginEditorLib
{
	SettingsMidiMatrix::SettingsMidiMatrix(SettingsMidi& _midi, Rml::Element* _root, const synthLib::MidiRoutingMatrix::EventType _type, std::string _name)
	: m_midi(_midi), m_eventType(_type), m_name(std::move(_name))
	{
		auto* headline = juceRmlUi::helper::findChild(_root, "matrixHeadline");
		headline->SetInnerRML(Rml::StringUtilities::EncodeRml(m_name));

		std::vector<juceRmlUi::ElemButton*> cells;

		juceRmlUi::helper::findChildren(cells, _root, "cell", CellCount * CellCount);

		for (size_t i=0; i<CellCount; ++i)
		{
			auto sourceType = Cells[i];

			for (size_t j=0; j<CellCount; ++j)
			{
				auto destType = Cells[j];

				auto button = cells[i * CellCount + j];

				juceRmlUi::EventListener::Add(button, Rml::EventId::Click, [this, button, sourceType, destType](Rml::Event& _event)
				{
					onClicked(_event, button, sourceType, destType);
				});

				m_cells[i][j] = button;

				button->SetProperty(Rml::PropertyId::PointerEvents, i != j ? Rml::Style::PointerEvents::Auto : Rml::Style::PointerEvents::None);
				button->SetPseudoClass("disabled", i == j);

				button->setChecked(m_midi.getProcessor().getMidiRoutingMatrix().enabled(sourceType, destType, m_eventType));
			}
		}
	}

	void SettingsMidiMatrix::refresh() const
	{
		auto& matrix = m_midi.getProcessor().getMidiRoutingMatrix();
		for (size_t i=0; i<CellCount; ++i)
		{
			auto sourceType = Cells[i];
			for (size_t j=0; j<CellCount; ++j)
			{
				auto destType = Cells[j];
				auto button = m_cells[i][j];
				button->setChecked(matrix.enabled(sourceType, destType, m_eventType));
			}
		}
	}

	void SettingsMidiMatrix::onClicked(Rml::Event& _event, juceRmlUi::ElemButton* _button, const synthLib::MidiEventSource _source, const synthLib::MidiEventSource _dest) const
	{
		_event.StopPropagation();
		auto& matrix = m_midi.getProcessor().getMidiRoutingMatrix();
		auto enable = !matrix.enabled(_source, _dest, m_eventType);
		matrix.setEnabled(_source, _dest, m_eventType, enable);
		_button->setChecked(enable);
	}
}

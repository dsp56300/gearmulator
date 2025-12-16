#pragma once

#include "synthLib/midiRoutingMatrix.h"

namespace juceRmlUi
{
	class ElemButton;
}

namespace Rml
{
	class Event;
	class Element;
}

namespace jucePluginEditorLib
{
	class SettingsMidi;

	class SettingsMidiMatrix
	{
	public:
		SettingsMidiMatrix(SettingsMidi& _midi, Rml::Element* _root, synthLib::MidiRoutingMatrix::EventType _type, std::string _name);

		void refresh() const;

	private:
		void onClicked(Rml::Event& _event, juceRmlUi::ElemButton* _button, synthLib::MidiEventSource _source, synthLib::MidiEventSource _dest) const;

		SettingsMidi& m_midi;
		const synthLib::MidiRoutingMatrix::EventType m_eventType;
		const std::string m_name;

		static constexpr synthLib::MidiEventSource Cells[] = {synthLib::MidiEventSource::Device, synthLib::MidiEventSource::Editor, synthLib::MidiEventSource::Host, synthLib::MidiEventSource::Physical};

		static constexpr uint32_t CellCount = std::size(Cells);

		using CellRow = std::array<juceRmlUi::ElemButton*, CellCount>;
		std::array<CellRow, CellCount> m_cells;
	};
}

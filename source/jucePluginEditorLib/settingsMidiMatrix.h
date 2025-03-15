#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

#include "synthLib/midiRoutingMatrix.h"

namespace jucePluginEditorLib
{
	class SettingsMidi;

	class SettingsMidiMatrix : public juce::Component
	{
	public:
		SettingsMidiMatrix(SettingsMidi& _midi, synthLib::MidiRoutingMatrix::EventType _type, std::string _name);

		void resized() override;

	private:
		void onClicked(const juce::ToggleButton* _button, synthLib::MidiEventSource _source, synthLib::MidiEventSource _dest) const;

		SettingsMidi& m_midi;
		const synthLib::MidiRoutingMatrix::EventType m_eventType;
		const std::string m_name;

		static constexpr synthLib::MidiEventSource Cells[] = {synthLib::MidiEventSource::Device, synthLib::MidiEventSource::Editor, synthLib::MidiEventSource::Host, synthLib::MidiEventSource::Physical};

		static constexpr uint32_t CellCount = std::size(Cells);

		using CellRow = std::array<std::unique_ptr<juce::ToggleButton>, CellCount>;
		std::array<CellRow, CellCount> m_cells;

		std::unique_ptr<juce::Label> m_headline;
		std::unique_ptr<juce::Label> m_corner;

		std::array<std::unique_ptr<juce::Label>, CellCount> m_sourceLabels;
		std::array<std::unique_ptr<juce::Label>, CellCount> m_destLabels;
	};
}

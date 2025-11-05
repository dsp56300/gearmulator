#include "settingsMidi.h"

#include "midiPorts.h"

#include "juceRmlUi/rmlElemComboBox.h"

namespace jucePluginEditorLib
{
	namespace settings
	{
		class Style;
	}

	SettingsMidi::SettingsMidi(Processor& _processor) : m_processor(_processor)
	{
		/*
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::Note, "Note"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::SysEx, "SysEx"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::Controller, "Controller"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::PolyPressure, "PolyPressure"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::Aftertouch, "Aftertouch"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::PitchBend, "PitchBend"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::ProgramChange, "ProgramChange"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::Other, "Other"));

		for (const auto& matrix : m_matrices)
			addAndMakeVisible(matrix.get());
		*/
	}

	SettingsMidi::~SettingsMidi() = default;

	void SettingsMidi::createUi(Rml::Element* _root)
	{
		juceRmlUi::ElemComboBox* comboInput = dynamic_cast<juceRmlUi::ElemComboBox*>(juceRmlUi::helper::findChild(_root, "midiInput"));
		juceRmlUi::ElemComboBox* comboOutput = dynamic_cast<juceRmlUi::ElemComboBox*>(juceRmlUi::helper::findChild(_root, "midiOutput"));

		if (comboInput)
			MidiPorts::initInputComboBox(m_processor, comboInput);
		if (comboOutput)
			MidiPorts::initOutputComboBox(m_processor, comboOutput);
	}
}

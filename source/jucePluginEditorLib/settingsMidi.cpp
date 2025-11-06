#include "settingsMidi.h"

#include "midiPorts.h"
#include "settingsMidiMatrix.h"

#include "juceRmlUi/rmlElemComboBox.h"
#include "synthLib/midiRoutingMatrix.h"

namespace jucePluginEditorLib
{
	namespace settings
	{
		class Style;
	}

	SettingsMidi::SettingsMidi(Processor& _processor) : m_processor(_processor)
	{
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

		auto* matrixElem = juceRmlUi::helper::findChild(_root, "matrix");

		auto newMatrixElem = [matrixElem, this]()
		{
			return matrixElem->GetParentNode()->AppendChild(matrixElem->Clone());
		};

		createMatrix(matrixElem, synthLib::MidiRoutingMatrix::EventType::Note, "Note On/Off");
		createMatrix(newMatrixElem(), synthLib::MidiRoutingMatrix::EventType::SysEx, "System Exclusive");
		createMatrix(newMatrixElem(), synthLib::MidiRoutingMatrix::EventType::Controller, "Controller");
		createMatrix(newMatrixElem(), synthLib::MidiRoutingMatrix::EventType::PolyPressure, "Poly Pressure");
		createMatrix(newMatrixElem(), synthLib::MidiRoutingMatrix::EventType::Aftertouch, "Aftertouch");
		createMatrix(newMatrixElem(), synthLib::MidiRoutingMatrix::EventType::PitchBend, "Pitch Bend");
		createMatrix(newMatrixElem(), synthLib::MidiRoutingMatrix::EventType::ProgramChange, "Program Change");
		createMatrix(newMatrixElem(), synthLib::MidiRoutingMatrix::EventType::Other, "Other");
	}

	void SettingsMidi::createMatrix(Rml::Element* _root, synthLib::MidiRoutingMatrix::EventType _type, const char* _name)
	{
		auto matrix = std::make_unique<SettingsMidiMatrix>(*this, _root, _type, _name);
		m_matrices.push_back(std::move(matrix));
	}
}

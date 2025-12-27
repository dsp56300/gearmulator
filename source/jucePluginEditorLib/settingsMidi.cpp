#include "settingsMidi.h"

#include "midiPorts.h"
#include "pluginProcessor.h"
#include "settingsMidiMatrix.h"
#include "baseLib/configFile.h"
#include "baseLib/filesystem.h"

#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlInplaceEditor.h"
#include "juceUiLib/messageBox.h"
#include "synthLib/midiRoutingMatrix.h"

namespace jucePluginEditorLib
{
	namespace settings
	{
		class Style;
	}

	constexpr std::string_view g_defaultPresetName = "Default";

	SettingsMidi::SettingsMidi(Processor& _processor) : m_processor(_processor)
	{
	}

	SettingsMidi::~SettingsMidi() = default;

	void SettingsMidi::createUi(Rml::Element* _root)
	{
		// Port Selection
		juceRmlUi::ElemComboBox* comboInput = dynamic_cast<juceRmlUi::ElemComboBox*>(juceRmlUi::helper::findChild(_root, "midiInput"));
		juceRmlUi::ElemComboBox* comboOutput = dynamic_cast<juceRmlUi::ElemComboBox*>(juceRmlUi::helper::findChild(_root, "midiOutput"));

		if (comboInput)
			MidiPorts::initInputComboBox(m_processor, comboInput);
		if (comboOutput)
			MidiPorts::initOutputComboBox(m_processor, comboOutput);

		// Panic
		addClickHandler(_root, "btPanicAllNotesOff", [this](Rml::Event&)
		{
			panicSendAllNotesOff();
		});

		addClickHandler(_root, "btPanicNoteOffEveryNote", [this](Rml::Event&)
		{
			panicSendNoteOffForEveryNote();
		});

		addClickHandler(_root, "btPanicReboot", [this](Rml::Event&)
		{
			panicRebootDevice();
		});

		// Matrix Presets
		m_presetList = juceRmlUi::helper::findChildT<juceRmlUi::ElemComboBox>(_root, "presetList");
		m_presetList->onValueChanged.addListener([this](float _value) { onPresetSelected(static_cast<int>(_value)); });

		auto* presetSave = juceRmlUi::helper::findChild(_root, "btPresetSave");
		auto* presetSaveAs = juceRmlUi::helper::findChild(_root, "btPresetSaveAs");
		auto* presetDelete = juceRmlUi::helper::findChild(_root, "btPresetDelete");

		juceRmlUi::EventListener::Add(presetSave, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onBtPresetSave(); });
		juceRmlUi::EventListener::Add(presetSaveAs, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onBtPresetSaveAs(); });
		juceRmlUi::EventListener::Add(presetDelete, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onBtPresetDelete(); });

		initPresetList();

		// Matrix
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

	void SettingsMidi::panicSendAllNotesOff() const
	{
		for(uint8_t c=0; c<16; ++c)
		{
			synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor, synthLib::M_CONTROLCHANGE + c, synthLib::MC_ALLNOTESOFF);
			m_processor.addMidiEvent(ev);
		}
	}

	void SettingsMidi::panicSendNoteOffForEveryNote() const
	{
		for(uint8_t c=0; c<16; ++c)
		{
			for(uint8_t n=0; n<128; ++n)
			{
				synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor, synthLib::M_NOTEOFF + c, n, 64, n * 256);
				m_processor.addMidiEvent(ev);
			}
		}
	}

	void SettingsMidi::panicRebootDevice() const
	{
		m_processor.rebootDevice();
	}

	void SettingsMidi::createMatrix(Rml::Element* _root, synthLib::MidiRoutingMatrix::EventType _type, const char* _name)
	{
		auto matrix = std::make_unique<SettingsMidiMatrix>(*this, _root, _type, _name);
		m_matrices.push_back(std::move(matrix));
	}

	void SettingsMidi::onBtPresetSave()
	{
		auto idx = m_presetList->getSelectedIndex();
		if (idx < 0 || idx >= static_cast<int>(m_presets.size()))
			return;
		auto name = m_presets[static_cast<size_t>(idx)].first;
		savePreset(name, false);
	}

	void SettingsMidi::onBtPresetSaveAs()
	{
		new juceRmlUi::InplaceEditor(m_presetList, "Enter name...", [this](const std::string& _name)
		{
			if (_name == g_defaultPresetName)
			{
				genericUI::MessageBox::showOk(
					genericUI::MessageBox::Icon::Warning, 
					m_processor.getProductName(), 
					"Cannot use reserved preset name '" + std::string(g_defaultPresetName) + "'.");
				return;
			}
			savePreset(_name);
		});
	}

	void SettingsMidi::onBtPresetDelete()
	{
		const auto idx = m_presetList->getSelectedIndex();

		if (idx < 0 || idx >= static_cast<int>(m_presets.size()))
			return;

		const auto name = m_presets[static_cast<size_t>(idx)].first;
		auto filename = getPresetFilename(name);

		if (name == g_defaultPresetName)
			return;

		genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Question, m_processor.getProductName(), "Delete preset '" + name + "'?", 
			[this, filename](const genericUI::MessageBox::Result _r) 
		{
			if (_r == genericUI::MessageBox::Result::Yes)
			{
				if (!baseLib::filesystem::remove(filename))
				{
					genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, m_processor.getProductName(), "Failed to delete preset file " + filename);
				}
				else
				{
					initPresetList();
				}
			}
		});	
	}

	void SettingsMidi::initPresetList()
	{
		m_presets.clear();
		m_presetList->clearOptions();

		m_presets.emplace_back(g_defaultPresetName, synthLib::MidiRoutingMatrix());
		m_presetList->addOption(std::string(g_defaultPresetName));

		std::vector<std::string> presetFiles;
		baseLib::filesystem::findFiles(presetFiles, m_processor.getConfigFolder(), ".mmcfg", 0, std::numeric_limits<size_t>::max());

		for (const auto& file : presetFiles)
		{
			auto presetName = baseLib::filesystem::stripExtension(baseLib::filesystem::getFilenameWithoutPath(file));

			synthLib::MidiRoutingMatrix m;

			if (m.readFromFile(file))
			{
				m_presets.emplace_back(presetName, m);
				m_presetList->addOption(presetName);
			}
		}

		for (size_t i=0; i<m_presets.size(); ++i)
		{
			if (m_presets[i].second == m_processor.getMidiRoutingMatrix())
			{
				m_presetList->setSelectedIndex(static_cast<int>(i));
				break;
			}
		}
	}

	void SettingsMidi::onPresetSelected(const int _index) const
	{
		if (_index < 0 || _index >= static_cast<int>(m_presets.size()))
			return;

		const auto p = m_presets[static_cast<size_t>(_index)].second;

		m_processor.getMidiRoutingMatrix() = p;

		for (auto& matrix : m_matrices)
		{
			matrix->refresh();
		}
	}

	std::string SettingsMidi::getPresetFilename(const std::string& _presetName) const
	{
		return m_processor.getConfigFolder() + _presetName + ".mmcfg";
	}

	void SettingsMidi::savePreset(const std::string& _name, bool _needsOverwriteConfirmation/* = true*/)
	{
		auto filename = getPresetFilename(_name);

		auto save = [this, filename]()
		{
			auto matrix = m_processor.getMidiRoutingMatrix();

			if (!matrix.writeToFile(filename))
				genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, m_processor.getProductName(), "Failed to write preset to file " + filename);

			initPresetList();
		};

		if (!_needsOverwriteConfirmation || !baseLib::filesystem::exists(filename))
		{
			save();
		}
		else
		{
			genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Question, m_processor.getProductName(), "Preset already exists. Overwrite?", 
				[this, save](const genericUI::MessageBox::Result _r) 
			{
				if (_r == genericUI::MessageBox::Result::Yes)
					save();
			});
		}
	}
}

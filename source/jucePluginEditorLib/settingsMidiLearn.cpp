#include "settingsMidiLearn.h"

#include "pluginProcessor.h"
#include "baseLib/filesystem.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInplaceEditor.h"
#include "juceUiLib/messageBox.h"

namespace jucePluginEditorLib
{
	SettingsMidiLearn::SettingsMidiLearn(Processor& _processor)
		: SettingsPlugin(_processor)
		, m_learnManager(juce::File(_processor.getConfigFolder() + "MidiLearn"))
	{
	}

	SettingsMidiLearn::~SettingsMidiLearn() = default;

	void SettingsMidiLearn::createUi(Rml::Element* _root)
	{
		// Preset management
		m_presetList = juceRmlUi::helper::findChildT<juceRmlUi::ElemComboBox>(_root, "presetList");
		m_presetList->onValueChanged.addListener([this](float _value) { onPresetSelected(static_cast<int>(_value)); });

		auto* btPresetCreate = juceRmlUi::helper::findChild(_root, "btPresetCreate");
		auto* btPresetDelete = juceRmlUi::helper::findChild(_root, "btPresetDelete");

		juceRmlUi::EventListener::Add(btPresetCreate, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onBtPresetCreate(); });
		juceRmlUi::EventListener::Add(btPresetDelete, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onBtPresetDelete(); });

		// Find the mapping table and template row
		auto* templateRow = juceRmlUi::helper::findChild(_root, "mappingRow");
		if (templateRow)
		{
			m_mappingTableBody = templateRow->GetParentNode();
			auto mappingRowTemplate = templateRow;
			
			// Remove the template row from view (header row at index 0 will remain)
			m_mappingRowTemplate = m_mappingTableBody->RemoveChild(mappingRowTemplate);
		}

		// Feedback checkboxes
		m_cbFeedbackDevice = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(_root, "cbFeedbackDevice");
		m_cbFeedbackEditor = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(_root, "cbFeedbackEditor");
		m_cbFeedbackHost = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(_root, "cbFeedbackHost");
		m_cbFeedbackPhysical = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(_root, "cbFeedbackPhysical");

		// Device checkbox is always disabled
		if (m_cbFeedbackDevice)
			m_cbFeedbackDevice->setChecked(false);

		juceRmlUi::EventListener::Add(m_cbFeedbackEditor, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onFeedbackTargetToggle(synthLib::MidiEventSource::Editor); });
		juceRmlUi::EventListener::Add(m_cbFeedbackHost, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onFeedbackTargetToggle(synthLib::MidiEventSource::Host); });
		juceRmlUi::EventListener::Add(m_cbFeedbackPhysical, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onFeedbackTargetToggle(synthLib::MidiEventSource::Physical); });

		initPresetList();
	}

	void SettingsMidiLearn::onBtPresetCreate()
	{
		new juceRmlUi::InplaceEditor(m_presetList, "Enter preset name...", [this](const std::string& _name)
		{
			if (_name.empty())
				return;

			const juce::String juceStr(_name);

			// Check if preset already exists
			if (std::find(m_presetNames.begin(), m_presetNames.end(), _name) != m_presetNames.end())
			{
				genericUI::MessageBox::showOk(
					genericUI::MessageBox::Icon::Warning,
					m_processor.getProductName(),
					"Preset '" + _name + "' already exists.");
				return;
			}

			// Create new preset
			pluginLib::MidiLearnPreset newPreset(_name);
			if (m_learnManager.savePreset(juceStr, newPreset))
			{
				initPresetList();
				
				// Select the newly created preset
				for (size_t i = 0; i < m_presetNames.size(); ++i)
				{
					if (m_presetNames[i] == _name)
					{
						m_presetList->setSelectedIndex(static_cast<int>(i));
						break;
					}
				}
			}
			else
			{
				genericUI::MessageBox::showOk(
					genericUI::MessageBox::Icon::Warning,
					m_processor.getProductName(),
					"Failed to create preset '" + _name + "'.");
			}
		});
	}

	void SettingsMidiLearn::onBtPresetDelete()
	{
		const auto idx = m_presetList->getSelectedIndex();
		if (idx < 0 || idx >= static_cast<int>(m_presetNames.size()))
			return;

		// Cannot delete the "Default" preset
		if (idx == 0)
			return;

		const auto& name = m_presetNames[static_cast<size_t>(idx)];

		genericUI::MessageBox::showYesNo(
			genericUI::MessageBox::Icon::Question,
			m_processor.getProductName(),
			"Delete preset '" + name + "'?",
			[this, name](const genericUI::MessageBox::Result _r)
			{
				if (_r == genericUI::MessageBox::Result::Yes)
				{
					if (m_learnManager.deletePreset(juce::String(name)))
					{
						initPresetList();
					}
					else
					{
						genericUI::MessageBox::showOk(
							genericUI::MessageBox::Icon::Warning,
							m_processor.getProductName(),
							"Failed to delete preset '" + name + "'.");
					}
				}
			});
	}

	void SettingsMidiLearn::onPresetSelected(const int _index)
	{
		if (_index < 0 || _index >= static_cast<int>(m_presetNames.size()))
			return;

		const auto& presetName = m_presetNames[static_cast<size_t>(_index)];
		auto* translator = m_processor.getMidiLearnTranslator();
		if (!translator)
			return;

		// Handle "Current" preset - don't load anything, just refresh display
		if (_index == 0 || presetName == "Current")
		{
			refreshMappingList();
			refreshFeedbackCheckboxes();
			return;
		}

		const juce::String juceStr(presetName);

		// Load the preset
		pluginLib::MidiLearnPreset preset(presetName);
		if (m_learnManager.loadPreset(juceStr, preset))
		{
			translator->setPreset(preset);
			refreshMappingList();
			refreshFeedbackCheckboxes();
		}
		else
		{
			genericUI::MessageBox::showOk(
				genericUI::MessageBox::Icon::Warning,
				m_processor.getProductName(),
				"Failed to load preset '" + presetName + "'.");
		}
	}

	void SettingsMidiLearn::onBtRemoveMapping(size_t _mappingIndex)
	{
		auto* translator = m_processor.getMidiLearnTranslator();
		if (!translator)
			return;

		auto& preset = const_cast<pluginLib::MidiLearnPreset&>(translator->getPreset());
		if (_mappingIndex >= preset.getMappings().size())
			return;

		preset.removeMapping(_mappingIndex);
		
		// Save the preset
		const juce::String presetName(preset.getName());
		if (m_learnManager.savePreset(presetName, preset))
		{
			translator->setPreset(preset); // Refresh subscriptions
			refreshMappingList();
			refreshFeedbackCheckboxes();
		}
		else
		{
			genericUI::MessageBox::showOk(
				genericUI::MessageBox::Icon::Warning,
				m_processor.getProductName(),
				"Failed to save preset changes.");
		}
	}

	void SettingsMidiLearn::onModeChanged(size_t _mappingIndex, int _newModeIndex)
	{
		auto* translator = m_processor.getMidiLearnTranslator();
		if (!translator)
			return;

		auto& preset = const_cast<pluginLib::MidiLearnPreset&>(translator->getPreset());
		auto& mappings = preset.getMappings();
		
		if (_mappingIndex >= mappings.size())
			return;

		// Update the mode (0 = Absolute, 1 = Relative)
		auto& mapping = mappings[_mappingIndex];
		mapping.mode = (_newModeIndex == 0) ? pluginLib::MidiLearnMapping::Mode::Absolute : pluginLib::MidiLearnMapping::Mode::Relative;

		// Save the preset
		const juce::String presetName(preset.getName());
		if (m_learnManager.savePreset(presetName, preset))
		{
			translator->setPreset(preset); // Refresh to apply changes
		}
		else
		{
			genericUI::MessageBox::showOk(
				genericUI::MessageBox::Icon::Warning,
				m_processor.getProductName(),
				"Failed to save mode change.");
		}
	}

	void SettingsMidiLearn::initPresetList()
	{
		m_presetNames.clear();
		m_presetList->clearOptions();

		// Always add "Current" as first option - represents current engine state
		m_presetNames.push_back("Current");
		m_presetList->addOption("Current");

		// Get all preset names
		auto jucePresets = m_learnManager.getPresetNames();
		for (const auto& juceName : jucePresets)
		{
			m_presetNames.push_back(juceName.toStdString());
		}

		// Populate combo box with loaded presets
		for (size_t i = 1; i < m_presetNames.size(); ++i)
		{
			m_presetList->addOption(m_presetNames[i]);
		}

		// Find if current translator preset matches any saved preset
		auto* translator = m_processor.getMidiLearnTranslator();
		int selectedIndex = 0; // Default to "Current"
		
		if (translator)
		{
			const auto& currentPreset = translator->getPreset();
			
			// Compare current preset to all saved presets
			for (size_t i = 1; i < m_presetNames.size(); ++i)
			{
				const auto& presetName = m_presetNames[i];
				pluginLib::MidiLearnPreset savedPreset(presetName);
				
				if (m_learnManager.loadPreset(juce::String(presetName), savedPreset))
				{
					if (currentPreset == savedPreset)
					{
						selectedIndex = static_cast<int>(i);
						break;
					}
				}
			}
		}
		
		// Set selected index without triggering callback that would overwrite preset
		// The callback will be triggered but onPresetSelected handles "Current" correctly
		m_presetList->setSelectedIndex(selectedIndex);
		
		refreshMappingList();
		refreshFeedbackCheckboxes();
	}

	void SettingsMidiLearn::refreshMappingList()
	{
		if (!m_mappingTableBody || !m_mappingRowTemplate)
			return;

		auto* translator = m_processor.getMidiLearnTranslator();
		if (!translator)
			return;

		// Clear existing rows but keep the header
		while (m_mappingTableBody->GetNumChildren() > 1)
		{
			m_mappingTableBody->RemoveChild(m_mappingTableBody->GetChild(1));
		}

		// Add a row for each mapping
		const auto& mappings = translator->getPreset().getMappings();
		for (size_t i = 0; i < mappings.size(); ++i)
		{
			const auto& mapping = mappings[i];

			// Clone the template row
			auto* row = m_mappingTableBody->AppendChild(m_mappingRowTemplate->Clone());

			// Fill in the mapping data
			auto* typeCell = juceRmlUi::helper::findChild(row, "type");
			auto* channelCell = juceRmlUi::helper::findChild(row, "channel");
			auto* controllerCell = juceRmlUi::helper::findChild(row, "controller");
			auto* modeCell = juceRmlUi::helper::findChild(row, "mode");
			auto* parameterCell = juceRmlUi::helper::findChild(row, "parameter");
			auto* removeButton = juceRmlUi::helper::findChild(row, "btRemove");

			if (typeCell)
			{
				std::string typeStr;
				switch (mapping.type)
				{
				case pluginLib::MidiLearnMapping::Type::ControlChange: typeStr = "CC"; break;
				case pluginLib::MidiLearnMapping::Type::PolyPressure: typeStr = "Poly Press"; break;
				case pluginLib::MidiLearnMapping::Type::ChannelPressure: typeStr = "Chan Press"; break;
				case pluginLib::MidiLearnMapping::Type::PitchBend: typeStr = "Pitch Bend"; break;
				case pluginLib::MidiLearnMapping::Type::NRPN: typeStr = "NRPN"; break;
				}
				typeCell->SetInnerRML(typeStr.c_str());
			}

			if (channelCell)
				channelCell->SetInnerRML(std::to_string(mapping.channel + 1).c_str());

			if (controllerCell)
				controllerCell->SetInnerRML(std::to_string(mapping.controller).c_str());

			// Create and populate mode combo box
			auto* modeCombo = juceRmlUi::helper::findChild(row, "modeCombo");
			if (modeCombo)
			{
				// Populate with both options
				auto* combobox = juceRmlUi::ElemComboBox::fromElement(modeCombo);
				if (combobox)
				{
					combobox->addItem("Absolute");
					combobox->addItem("Relative");
					
					// Set current mode
					const int currentMode = (mapping.mode == pluginLib::MidiLearnMapping::Mode::Absolute) ? 0 : 1;
					combobox->setSelectedIndex(currentMode);
					
					// Handle mode change
					combobox->onValueChanged = [this, i](int _newIndex)
					{
						onModeChanged(i, _newIndex);
					};
				}
			}

			if (parameterCell)
				parameterCell->SetInnerRML(mapping.paramName.c_str());

			if (removeButton)
			{
				juceRmlUi::EventListener::Add(removeButton, Rml::EventId::Click, [this, i](Rml::Event& _event)
				{
					_event.StopPropagation();
					onBtRemoveMapping(i);
				});
			}
		}

		// Update feedback checkboxes (for first mapping if exists)
		refreshFeedbackCheckboxes();
	}

	void SettingsMidiLearn::refreshFeedbackCheckboxes()
	{
		auto* translator = m_processor.getMidiLearnTranslator();
		if (!translator)
			return;

		// For now, we show feedback settings globally for all mappings
		// (In future, could show per-mapping if we add a selection mechanism)
		const auto& mappings = translator->getPreset().getMappings();
		
		// If no mappings, disable all checkboxes
		if (mappings.empty())
		{
			if (m_cbFeedbackEditor) m_cbFeedbackEditor->setChecked(false);
			if (m_cbFeedbackHost) m_cbFeedbackHost->setChecked(false);
			if (m_cbFeedbackPhysical) m_cbFeedbackPhysical->setChecked(false);
			return;
		}

		// Show feedback settings from first mapping
		// (All mappings share feedback settings in this simple version)
		const auto& firstMapping = mappings[0];
		
		if (m_cbFeedbackEditor)
			m_cbFeedbackEditor->setChecked(firstMapping.isFeedbackEnabled(synthLib::MidiEventSource::Editor));
		
		if (m_cbFeedbackHost)
			m_cbFeedbackHost->setChecked(firstMapping.isFeedbackEnabled(synthLib::MidiEventSource::Host));
		
		if (m_cbFeedbackPhysical)
			m_cbFeedbackPhysical->setChecked(firstMapping.isFeedbackEnabled(synthLib::MidiEventSource::Physical));
	}

	void SettingsMidiLearn::onFeedbackTargetToggle(synthLib::MidiEventSource _target)
	{
		auto* translator = m_processor.getMidiLearnTranslator();
		if (!translator)
			return;

		auto& preset = const_cast<pluginLib::MidiLearnPreset&>(translator->getPreset());
		auto& mappings = preset.getMappings();
		
		if (mappings.empty())
			return;

		// Toggle feedback target for ALL mappings
		const bool newState = !mappings[0].isFeedbackEnabled(_target);
		
		for (auto& mapping : mappings)
		{
			mapping.setFeedbackEnabled(_target, newState);
		}

		// Save the updated preset
		const juce::String presetName(preset.getName());
		if (!presetName.isEmpty())
		{
			m_learnManager.savePreset(presetName, preset);
		}

		// Refresh subscriptions with updated preset
		translator->setPreset(preset);

		refreshFeedbackCheckboxes();
	}
}

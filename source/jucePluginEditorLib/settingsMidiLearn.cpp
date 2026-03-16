#include "settingsMidiLearn.h"

#include "pluginProcessor.h"
#include "baseLib/filesystem.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInplaceEditor.h"
#include "juceUiLib/messageBox.h"

namespace jucePluginEditorLib
{
	static constexpr const char* kCurrentPresetName = "Current";

	SettingsMidiLearn::SettingsMidiLearn(Processor& _processor)
		: SettingsPlugin(_processor)
		, m_learnManager(juce::File(_processor.getMidiLearnFolder()))
	{
	}

	SettingsMidiLearn::~SettingsMidiLearn()
	{
		// If preset was not applied and we have original state, restore it
		if (!m_originalPreset.empty() && !m_presetApplied)
		{
			auto* translator = m_processor.getMidiLearnTranslator();
			if (translator)
			{
				translator->setPreset(m_originalPreset);
			}
		}
	}

	void SettingsMidiLearn::createUi(Rml::Element* _root)
	{
		// Save the current preset state before any changes
		auto* translator = m_processor.getMidiLearnTranslator();
		if (translator)
		{
			m_originalPreset = translator->getPreset();
			m_presetApplied = false;
		}

		// Preset management
		m_presetList = juceRmlUi::helper::findChildT<juceRmlUi::ElemComboBox>(_root, "presetList");
		m_presetList->onValueChanged.addListener([this](float _value) { onPresetSelected(static_cast<int>(_value)); });

		auto* btPresetCreate = juceRmlUi::helper::findChild(_root, "btPresetCreate");
		auto* btPresetDelete = juceRmlUi::helper::findChild(_root, "btPresetDelete");
		m_btApply = juceRmlUi::helper::findChild(_root, "btPresetApply");

		juceRmlUi::EventListener::Add(btPresetCreate, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onBtPresetCreate(); });
		juceRmlUi::EventListener::Add(btPresetDelete, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onBtPresetDelete(); });
		juceRmlUi::EventListener::Add(m_btApply, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onBtPresetApply(); });

		if (auto* btBrowsePresets = juceRmlUi::helper::findChild(_root, "btBrowsePresets"))
		{
			juceRmlUi::EventListener::Add(btBrowsePresets, Rml::EventId::Click, [this](Rml::Event& _event)
			{
				_event.StopPropagation();
				const juce::File dir = m_learnManager.getPresetsDirectory();
				baseLib::filesystem::createDirectory(dir.getFullPathName().toStdString());
				dir.revealToUser();
			});
		}

		// Find the mapping table and template row
		auto* templateRow = juceRmlUi::helper::findChild(_root, "mappingRow");
		if (templateRow)
		{
			m_mappingTableBody = templateRow->GetParentNode();
			auto mappingRowTemplate = templateRow;
			
			// Remove the template row from view (header row at index 0 will remain)
			m_mappingRowTemplate = m_mappingTableBody->RemoveChild(mappingRowTemplate);
		}

		// Input source checkboxes
		m_cbInputHost = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(_root, "cbInputHost");
		m_cbInputPhysical = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(_root, "cbInputPhysical");

		juceRmlUi::EventListener::Add(m_cbInputHost, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onInputSourceToggle(synthLib::MidiEventSource::Host); });
		juceRmlUi::EventListener::Add(m_cbInputPhysical, Rml::EventId::Click, [this](Rml::Event& _event) { _event.StopPropagation(); onInputSourceToggle(synthLib::MidiEventSource::Physical); });

		// Feedback checkboxes
		m_cbFeedbackHost = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(_root, "cbFeedbackHost");
		m_cbFeedbackPhysical = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(_root, "cbFeedbackPhysical");

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

			// Get current preset from translator (copy it to new preset)
			auto* translator = m_processor.getMidiLearnTranslator();
			if (!translator)
				return;

			// Copy current preset and give it the new name
			pluginLib::MidiLearnPreset newPreset = translator->getPreset();
			newPreset.setName(_name);
			
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

		// Cannot delete the "Current" preset
		if (isCurrentPresetSelected())
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

		// Handle "Current" preset - restore original and just refresh display
		if (_index == 0 || presetName == kCurrentPresetName)
		{
			// Restore the original preset if we had saved it
			if (!m_originalPreset.empty())
			{
				translator->setPreset(m_originalPreset);
			}
			refreshMappingList();
			refreshInputSourceCheckboxes();
			refreshFeedbackCheckboxes();
			updateApplyButtonVisibility();
			return;
		}

		const juce::String juceStr(presetName);

		// Load the preset (for testing/preview)
		pluginLib::MidiLearnPreset preset(presetName);
		if (m_learnManager.loadPreset(juceStr, preset))
		{
			translator->setPreset(preset);
			refreshMappingList();
			refreshInputSourceCheckboxes();
			refreshFeedbackCheckboxes();
			updateApplyButtonVisibility();
		}
		else
		{
			genericUI::MessageBox::showOk(
				genericUI::MessageBox::Icon::Warning,
				m_processor.getProductName(),
				"Failed to load preset '" + presetName + "'.");
		}
	}

	void SettingsMidiLearn::onBtPresetApply()
	{
		if (isCurrentPresetSelected())
			return;

		const int idx = m_presetList->getSelectedIndex();
		if (idx < 0 || idx >= static_cast<int>(m_presetNames.size()))
			return;

		const auto& presetName = m_presetNames[static_cast<size_t>(idx)];

		genericUI::MessageBox::showYesNo(
			genericUI::MessageBox::Icon::Question,
			m_processor.getProductName(),
			"Apply preset '" + presetName + "'?\n\nThis will overwrite the current mappings.",
			[this](const genericUI::MessageBox::Result _r)
			{
				if (_r == genericUI::MessageBox::Result::Yes)
				{
					// Mark as applied so destructor won't restore
					m_presetApplied = true;
					m_originalPreset = pluginLib::MidiLearnPreset(); // clear
					
					// Update the original preset to current state (so it's now the baseline)
					auto* translator = m_processor.getMidiLearnTranslator();
					if (translator)
					{
						m_originalPreset = translator->getPreset();
						m_presetApplied = false;
					}
					
					updateApplyButtonVisibility();
				}
			});
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
			if (isCurrentPresetSelected())
				m_originalPreset = preset;
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

		// Validate mode index and update the mode
		if (_newModeIndex < 0 || _newModeIndex >= static_cast<int>(pluginLib::MidiLearnMapping::Mode::Count))
			return;

		auto& mapping = mappings[_mappingIndex];
		mapping.mode = static_cast<pluginLib::MidiLearnMapping::Mode>(_newModeIndex);

		// Save the preset
		const juce::String presetName(preset.getName());
		if (m_learnManager.savePreset(presetName, preset))
		{
			translator->setPreset(preset); // Refresh to apply changes
			if (isCurrentPresetSelected())
				m_originalPreset = preset;
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
		m_presetNames.emplace_back(kCurrentPresetName);
		m_presetList->addOption(kCurrentPresetName);

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
		refreshInputSourceCheckboxes();
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
				typeCell->SetInnerRML(typeStr);
			}

			if (channelCell)
				channelCell->SetInnerRML(std::to_string(mapping.channel + 1));

			if (controllerCell)
			{
				if (mapping.type == pluginLib::MidiLearnMapping::Type::ChannelPressure ||
				    mapping.type == pluginLib::MidiLearnMapping::Type::PitchBend)
					controllerCell->SetInnerRML("-");
				else
					controllerCell->SetInnerRML(std::to_string(mapping.controller));
			}

			// Create and populate mode combo box
			if (auto* modeCombo = juceRmlUi::helper::findChildT<juceRmlUi::ElemComboBox>(row, "modeCombo"))
			{
				// Populate with all mode options
				for (const auto* modeStr : pluginLib::MidiLearnMapping::ModeStrings)
				{
					modeCombo->addOption(modeStr);
				}
				
				// Set current mode
				const int currentMode = static_cast<int>(mapping.mode);
				modeCombo->setSelectedIndex(currentMode, false); // Don't trigger callback when setting initial value
				
				// Handle mode change
				modeCombo->onValueChanged.addListener([this, i](float _value)
				{
					onModeChanged(i, static_cast<int>(_value));
				});
			}

			if (parameterCell)
				parameterCell->SetInnerRML(mapping.paramName);

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

	void SettingsMidiLearn::refreshFeedbackCheckboxes() const
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
			if (m_cbFeedbackHost) m_cbFeedbackHost->setChecked(false);
			if (m_cbFeedbackPhysical) m_cbFeedbackPhysical->setChecked(false);
			return;
		}

		// Show feedback settings from first mapping
		// (All mappings share feedback settings in this simple version)
		const auto& firstMapping = mappings[0];
		
		if (m_cbFeedbackHost)
			m_cbFeedbackHost->setChecked(firstMapping.isFeedbackEnabled(synthLib::MidiEventSource::Host));
		
		if (m_cbFeedbackPhysical)
			m_cbFeedbackPhysical->setChecked(firstMapping.isFeedbackEnabled(synthLib::MidiEventSource::Physical));
	}

	void SettingsMidiLearn::refreshInputSourceCheckboxes() const
	{
		auto* translator = m_processor.getMidiLearnTranslator();
		if (!translator)
			return;

		// Get current input sources from translator
		const uint8_t sources = translator->getLearnInputSources();
		
		if (m_cbInputHost)
			m_cbInputHost->setChecked(sources & (1<<static_cast<uint8_t>(synthLib::MidiEventSource::Host)));
		
		if (m_cbInputPhysical)
			m_cbInputPhysical->setChecked(sources & (1<<static_cast<uint8_t>(synthLib::MidiEventSource::Physical)));
	}

	void SettingsMidiLearn::onInputSourceToggle(synthLib::MidiEventSource _source) const
	{
		auto* translator = m_processor.getMidiLearnTranslator();
		if (!translator)
			return;

		// Toggle the input source
		const uint8_t currentSources = translator->getLearnInputSources();
		const uint8_t sourceBit = static_cast<uint8_t>(1 << static_cast<uint8_t>(_source));
		const uint8_t newSources = currentSources ^ sourceBit; // XOR to toggle
		
		translator->setLearnInputSources(newSources);
		
		refreshInputSourceCheckboxes();
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
		if (isCurrentPresetSelected())
			m_originalPreset = preset;

		refreshFeedbackCheckboxes();
	}

	void SettingsMidiLearn::updateApplyButtonVisibility() const
	{
		if (!m_btApply)
			return;

		const bool isCurrent = isCurrentPresetSelected();

		// Show Apply button only when a non-Current preset is selected
		if (isCurrent)
		{
			m_btApply->SetProperty("display", "none");
		}
		else
		{
			m_btApply->SetProperty("display", "inline-block");
		}
	}

	bool SettingsMidiLearn::isCurrentPresetSelected() const
	{
		return m_presetList && m_presetList->getSelectedIndex() == 0;
	}
}

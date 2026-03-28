#pragma once

#include "settingsPlugin.h"
#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlElemButton.h"
#include "jucePluginLib/midiLearnPreset.h"
#include "jucePluginLib/midiLearnManager.h"

namespace pluginLib
{
	class Processor;
}

namespace jucePluginEditorLib
{
	class SettingsMidiLearn : public SettingsPlugin
	{
	public:
		SettingsMidiLearn(Processor& _processor);
		~SettingsMidiLearn() override;

		std::string getCategoryName() const override { return "MIDI Learn"; }
		std::string getTemplateName() const override { return "tus_settings_midilearn"; }

		void createUi(Rml::Element* _root) override;

	private:
		void onBtPresetCreate();
		void onBtPresetDelete();
		void onBtPresetApply();
		void onPresetSelected(int _index);
		void onBtRemoveMapping(size_t _mappingIndex);
		void onModeChanged(size_t _mappingIndex, int _newModeIndex);
		void onChannelChanged(size_t _mappingIndex, int _newChannelIndex);
		void onPartChanged(size_t _mappingIndex, int _newPartIndex);
		void onInputSourceToggle(synthLib::MidiEventSource _source) const;
		void onFeedbackTargetToggle(synthLib::MidiEventSource _target);

		void initPresetList();
		void refreshMappingList();
		void refreshInputSourceCheckboxes() const;
		void refreshFeedbackCheckboxes() const;
		void updateApplyButtonVisibility() const;
		bool isCurrentPresetSelected() const;

		pluginLib::MidiLearnManager m_learnManager;
		juceRmlUi::ElemComboBox* m_presetList = nullptr;
		Rml::Element* m_mappingTableBody = nullptr;
		Rml::ElementPtr m_mappingRowTemplate = nullptr;
		Rml::Element* m_btApply = nullptr;
		
		// Input source checkboxes
		juceRmlUi::ElemButton* m_cbInputHost = nullptr;
		juceRmlUi::ElemButton* m_cbInputPhysical = nullptr;
		
		// Feedback checkboxes
		juceRmlUi::ElemButton* m_cbFeedbackHost = nullptr;
		juceRmlUi::ElemButton* m_cbFeedbackPhysical = nullptr;
		
		std::vector<std::string> m_presetNames;
		
		// Saved state for restore on cancel
		pluginLib::MidiLearnPreset m_originalPreset;
		bool m_presetApplied = false;
	};
}

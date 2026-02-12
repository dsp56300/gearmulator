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
		void onPresetSelected(int _index);
		void onBtRemoveMapping(size_t _mappingIndex);
		void onModeChanged(size_t _mappingIndex, int _newModeIndex);
		void onFeedbackTargetToggle(synthLib::MidiEventSource _target);

		void initPresetList();
		void refreshMappingList();
		void refreshFeedbackCheckboxes();

		pluginLib::MidiLearnManager m_learnManager;
		juceRmlUi::ElemComboBox* m_presetList = nullptr;
		Rml::Element* m_mappingTableBody = nullptr;
		Rml::ElementPtr m_mappingRowTemplate = nullptr;
		
		// Feedback checkboxes
		juceRmlUi::ElemButton* m_cbFeedbackDevice = nullptr;
		juceRmlUi::ElemButton* m_cbFeedbackEditor = nullptr;
		juceRmlUi::ElemButton* m_cbFeedbackHost = nullptr;
		juceRmlUi::ElemButton* m_cbFeedbackPhysical = nullptr;
		
		std::vector<std::string> m_presetNames;
		size_t m_selectedMappingIndex = static_cast<size_t>(-1);
	};
}

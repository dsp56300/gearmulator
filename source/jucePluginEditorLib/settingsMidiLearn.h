#pragma once

#include "settingsPlugin.h"
#include "juceRmlUi/rmlElemComboBox.h"
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

		void initPresetList();
		void refreshMappingList();

		pluginLib::MidiLearnManager m_learnManager;
		juceRmlUi::ElemComboBox* m_presetList = nullptr;
		Rml::Element* m_mappingTableBody = nullptr;
		Rml::Element* m_mappingRowTemplate = nullptr;
		
		std::vector<std::string> m_presetNames;
		pluginLib::MidiLearnPreset m_currentPreset;
	};
}

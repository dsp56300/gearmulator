#pragma once

#include "pluginEditor.h"

#include "settingsPlugin.h"
#include "juceRmlUi/rmlElemComboBox.h"
#include "synthLib/midiRoutingMatrix.h"

namespace pluginLib
{
	class Processor;
}

namespace jucePluginEditorLib
{
	class SettingsMidiMatrix;

	class SettingsMidi : public SettingsPlugin
	{
	public:
		SettingsMidi(Processor& _processor);
		~SettingsMidi() override;

		Processor& getProcessor() const { return m_processor; }

		std::string getCategoryName() const override { return "MIDI"; }
		std::string getTemplateName() const override { return "tus_settings_midi"; }

		void createUi(Rml::Element* _root) override;

	private:
		void panicSendAllNotesOff() const;
		void panicSendNoteOffForEveryNote() const;
		void panicRebootDevice() const;

		void createMatrix(Rml::Element* _root, synthLib::MidiRoutingMatrix::EventType _type, const char* _name);

		void onBtPresetSave();
		void onBtPresetSaveAs();
		void onBtPresetDelete();

		void initPresetList();

		void onPresetSelected(int _index) const;

		std::string getPresetFilename(const std::string& _presetName) const;

		void savePreset(const std::string& _name, bool _needsOverwriteConfirmation = true);

		Processor& m_processor;

		std::vector<std::unique_ptr<SettingsMidiMatrix>> m_matrices;
		juceRmlUi::ElemComboBox* m_presetList = nullptr;
		std::vector<std::pair<std::string, synthLib::MidiRoutingMatrix>> m_presets;
	};
}

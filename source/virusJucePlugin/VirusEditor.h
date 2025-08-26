#pragma once

#include "jucePluginEditorLib/midiPorts.h"
#include "jucePluginEditorLib/pluginEditor.h"
#include "jucePluginEditorLib/focusedParameter.h"

#include "baseLib/event.h"

#include "Parts.h"
#include "PatchManager.h"
#include "Leds.h"

namespace juceRmlUi
{
	class ElemComboBox;
}

namespace virusLib
{
	class ROMFile;
}

namespace jucePluginEditorLib
{
	class FocusedParameter;
}

namespace pluginLib
{
	class Parameter;
}

namespace virus
{
	class VirusProcessor;
}

namespace genericVirusUI
{
	class ArpUserPattern;

	class VirusEditor : public jucePluginEditorLib::Editor
	{
	public:
		enum class SaveType
		{
			CurrentSingle,
			Bank,
			Arrangement
		};

		VirusEditor(virus::VirusProcessor& _processorRef, const jucePluginEditorLib::Skin& _skin);
		~VirusEditor() override;

		void create() override;

		void setPart(size_t _part);

		virus::Controller& getController() const;

		std::pair<std::string, std::string> getDemoRestrictionText() const override;

		const auto& getLeds() const { return m_leds; }

		void initSkinConverterOptions(rmlPlugin::skinConverter::SkinConverterOptions&) override;
		void initPluginDataModel(jucePluginEditorLib::PluginDataModel& _model) override;

		jucePluginEditorLib::patchManager::PatchManager* createPatchManager(juceRmlUi::RmlComponent& _rmlCompnent, Rml::Element* _parent) override;

	private:
		void onProgramChange(int _part);
		void onPlayModeChanged();
		void onCurrentPartChanged();

		void updatePresetName() const;
		void updatePlayModeButtons() const;

		void updateDeviceModel();

		void savePreset(Rml::Event& _event);
		void loadPreset();

		void setPlayMode(uint8_t _playMode);

		void savePresets(SaveType _saveType, const pluginLib::FileType& _fileType, uint8_t _bankNumber = 0);
		bool savePresets(const std::string& _pathName, SaveType _saveType, const pluginLib::FileType& _fileType, uint8_t _bankNumber = 0) const;

		virus::VirusProcessor& m_processor;

		std::unique_ptr<Parts> m_parts;
		std::unique_ptr<Leds> m_leds;
		std::unique_ptr<jucePluginEditorLib::MidiPorts> m_midiPorts;

		Rml::Element* m_presetName = nullptr;
//		PartMouseListener* m_presetNameMouseListener  = nullptr;

		std::unique_ptr<jucePluginEditorLib::FocusedParameter> m_focusedParameter;

		juceRmlUi::ElemComboBox* m_romSelector = nullptr;

		juceRmlUi::ElemButton* m_playModeSingle = nullptr;
		juceRmlUi::ElemButton* m_playModeMulti = nullptr;
		juceRmlUi::ElemButton* m_playModeToggle = nullptr;

		Rml::Element* m_deviceModel = nullptr;

		std::unique_ptr<ArpUserPattern> m_arpUserPattern;

		baseLib::EventListener<const virusLib::ROMFile*> m_romChangedListener;
	};
}

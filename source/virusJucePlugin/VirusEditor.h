#pragma once

#include "jucePluginEditorLib/midiPorts.h"
#include "jucePluginEditorLib/pluginEditor.h"
#include "jucePluginEditorLib/focusedParameter.h"

#include "jucePluginLib/event.h"

#include "Parts.h"
#include "Tabs.h"
#include "FxPage.h"
#include "PatchManager.h"
#include "ControllerLinks.h"
#include "Leds.h"

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
	class ParameterBinding;
}

class VirusProcessor;

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

		VirusEditor(pluginLib::ParameterBinding& _binding, VirusProcessor& _processorRef, const std::string& _jsonFilename, std::string _skinFolder);
		~VirusEditor() override;

		void setPart(size_t _part);

		pluginLib::ParameterBinding& getParameterBinding() const { return m_parameterBinding; }

		Virus::Controller& getController() const;

		const char* findEmbeddedResource(const std::string& _filename, uint32_t& _size) const;
		const char* findResourceByFilename(const std::string& _filename, uint32_t& _size) override;

		std::pair<std::string, std::string> getDemoRestrictionText() const override;

		genericUI::Button<juce::TextButton>* createJuceComponent(genericUI::Button<juce::TextButton>*, genericUI::UiObject& _object) override;
		juce::Component* createJuceComponent(juce::Component*, genericUI::UiObject& _object) override;

		const auto& getLeds() const { return m_leds; }

	private:
		void onProgramChange(int _part);
		void onPlayModeChanged();
		void onCurrentPartChanged();

		void mouseEnter(const juce::MouseEvent& event) override;

		void updatePresetName() const;
		void updatePlayModeButtons() const;

		void updateDeviceModel();

		void savePreset();
		void loadPreset();

		void setPlayMode(uint8_t _playMode);

		void savePresets(SaveType _saveType, jucePluginEditorLib::FileType _fileType, uint8_t _bankNumber = 0);
		bool savePresets(const std::string& _pathName, SaveType _saveType, jucePluginEditorLib::FileType _fileType, uint8_t _bankNumber = 0) const;

		VirusProcessor& m_processor;
		pluginLib::ParameterBinding& m_parameterBinding;

		std::unique_ptr<Parts> m_parts;
		std::unique_ptr<Leds> m_leds;
		std::unique_ptr<Tabs> m_tabs;
		std::unique_ptr<jucePluginEditorLib::MidiPorts> m_midiPorts;
		std::unique_ptr<FxPage> m_fxPage;
		std::unique_ptr<ControllerLinks> m_controllerLinks;

		juce::Label* m_presetName = nullptr;
		PartMouseListener* m_presetNameMouseListener  = nullptr;

		std::unique_ptr<jucePluginEditorLib::FocusedParameter> m_focusedParameter;

		juce::ComboBox* m_romSelector = nullptr;

		juce::Button* m_playModeSingle = nullptr;
		juce::Button* m_playModeMulti = nullptr;
		juce::Button* m_playModeToggle = nullptr;

		juce::Label* m_deviceModel = nullptr;

		ArpUserPattern* m_arpUserPattern = nullptr;

		pluginLib::EventListener<const virusLib::ROMFile*> m_romChangedListener;
	};
}

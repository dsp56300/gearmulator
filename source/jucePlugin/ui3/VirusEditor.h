#pragma once

#include "../../jucePluginEditorLib/midiPorts.h"
#include "../../jucePluginEditorLib/pluginEditor.h"
#include "../../jucePluginEditorLib/focusedParameter.h"

#include "Parts.h"
#include "Tabs.h"
#include "FxPage.h"
#include "PatchBrowser.h"
#include "ControllerLinks.h"

namespace jucePluginEditorLib
{
	class FocusedParameter;
}

namespace pluginLib
{
	class Parameter;
	class ParameterBinding;
}

class AudioPluginAudioProcessor;

namespace genericVirusUI
{
	class VirusEditor : public jucePluginEditorLib::Editor
	{
	public:
		enum class SaveType
		{
			CurrentSingle,
			Bank,
			Arrangement
		};

		VirusEditor(pluginLib::ParameterBinding& _binding, AudioPluginAudioProcessor& _processorRef, const std::string& _jsonFilename,
		            std::string _skinFolder, std::function<void()> _openMenuCallback);
		~VirusEditor() override;

		void setPart(size_t _part);

		AudioPluginAudioProcessor& getProcessor() const { return m_processor; }
		pluginLib::ParameterBinding& getParameterBinding() const { return m_parameterBinding; }

		Virus::Controller& getController() const;

		static const char* findEmbeddedResource(const std::string& _filename, uint32_t& _size);
		const char* findResourceByFilename(const std::string& _filename, uint32_t& _size) override;

		PatchBrowser* getPatchBrowser();

		std::pair<std::string, std::string> getDemoRestrictionText() const override;

	private:
		void onProgramChange();
		void onPlayModeChanged();
		void onCurrentPartChanged();

		void mouseEnter(const juce::MouseEvent& event) override;

		void updatePresetName() const;
		void updatePlayModeButtons() const;

		void updateDeviceModel();

		void savePreset();
		void loadPreset();

		void setPlayMode(uint8_t _playMode);

		void savePresets(SaveType _saveType, FileType _fileType, uint8_t _bankNumber = 0);
		bool savePresets(const std::string& _pathName, SaveType _saveType, FileType _fileType, uint8_t _bankNumber = 0) const;

		AudioPluginAudioProcessor& m_processor;
		pluginLib::ParameterBinding& m_parameterBinding;

		std::unique_ptr<Parts> m_parts;
		std::unique_ptr<Tabs> m_tabs;
		std::unique_ptr<jucePluginEditorLib::MidiPorts> m_midiPorts;
		std::unique_ptr<FxPage> m_fxPage;
		std::unique_ptr<PatchBrowser> m_patchBrowser;
		std::unique_ptr<ControllerLinks> m_controllerLinks;

		juce::Label* m_presetName = nullptr;

		std::unique_ptr<jucePluginEditorLib::FocusedParameter> m_focusedParameter;

		juce::ComboBox* m_romSelector = nullptr;

		juce::Button* m_playModeSingle = nullptr;
		juce::Button* m_playModeMulti = nullptr;
		juce::Button* m_playModeToggle = nullptr;

		juce::Label* m_deviceModel = nullptr;

		juce::TooltipWindow m_tooltipWindow;

		std::function<void()> m_openMenuCallback;
	};
}

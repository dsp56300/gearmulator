#pragma once

#include "../../juceUiLib/editor.h"

#include "Parts.h"
#include "Tabs.h"
#include "FxPage.h"
#include "MidiPorts.h"
#include "PatchBrowser.h"

class VirusParameterBinding;
class AudioPluginAudioProcessor;

namespace genericVirusUI
{
	class VirusEditor : public genericUI::EditorInterface, public genericUI::Editor
	{
	public:
		enum class FileType
		{
			Syx,
			Mid
		};

		enum class SaveType
		{
			CurrentSingle,
			Bank,
			Arrangement
		};

		VirusEditor(VirusParameterBinding& _binding, AudioPluginAudioProcessor &_processorRef, const std::string& _jsonFilename,
		            std::string _skinFolder, std::function<void()> _openMenuCallback);
		~VirusEditor() override;

		void setPart(size_t _part);

		AudioPluginAudioProcessor& getProcessor() const { return m_processor; }
		VirusParameterBinding& getParameterBinding() const { return m_parameterBinding; }

		Virus::Controller& getController() const;

		static const char* findNamedResourceByFilename(const std::string& _filename, uint32_t& _size);

		PatchBrowser* getPatchBrowser();

	private:
		const char* getResourceByFilename(const std::string& _name, uint32_t& _dataSize) override;
		int getParameterIndexByName(const std::string& _name) override;
		bool bindParameter(juce::Button& _target, int _parameterIndex) override;
		bool bindParameter(juce::ComboBox& _target, int _parameterIndex) override;
		bool bindParameter(juce::Slider& _target, int _parameterIndex) override;
		juce::Value* getParameterValue(int _parameterIndex) override;

		void onProgramChange();
		void onPlayModeChanged();
		void onCurrentPartChanged();

		void mouseDrag(const juce::MouseEvent& event) override;
		void mouseEnter(const juce::MouseEvent& event) override;
		void mouseExit(const juce::MouseEvent& event) override;
		void mouseUp(const juce::MouseEvent& event) override;

		void updateControlLabel(juce::Component* _component) const;
		void updatePresetName() const;
		void updatePlayModeButtons() const;

		void updateDeviceModel();

		void savePreset();
		void loadPreset();

		void setPlayMode(uint8_t _playMode);

		void savePresets(SaveType _saveType, FileType _fileType, uint8_t _bankNumber = 0);
		bool savePresets(const std::string& _pathName, SaveType _saveType, FileType _fileType, uint8_t _bankNumber = 0) const;

		AudioPluginAudioProcessor& m_processor;
		VirusParameterBinding& m_parameterBinding;

		const std::string m_skinFolder;

		std::unique_ptr<Parts> m_parts;
		std::unique_ptr<Tabs> m_tabs;
		std::unique_ptr<MidiPorts> m_midiPorts;
		std::unique_ptr<FxPage> m_fxPage;
		std::unique_ptr<PatchBrowser> m_patchBrowser;

		juce::Label* m_presetName = nullptr;
		juce::Label* m_focusedParameterName = nullptr;
		juce::Label* m_focusedParameterValue = nullptr;
		juce::Label* m_focusedParameterTooltip = nullptr;
		juce::ComboBox* m_romSelector = nullptr;

		juce::Button* m_playModeSingle = nullptr;
		juce::Button* m_playModeMulti = nullptr;
		juce::Button* m_playModeToggle = nullptr;

		juce::Label* m_deviceModel = nullptr;

		juce::TooltipWindow m_tooltipWindow;

		std::unique_ptr<juce::FileChooser> m_fileChooser;
		juce::String m_previousPath;
		std::function<void()> m_openMenuCallback;

		std::map<std::string, std::vector<char>> m_fileCache;
	};
}

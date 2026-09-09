#pragma once

#include "Emu88Processor.h"

#include "baseLib/event.h"

#include "juceRmlUi/rmlDataProvider.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "juce_audio_plugin_client/Standalone/juce_StandaloneOptionsMenuHandler.h"
#include "juce_audio_processors/juce_audio_processors.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Rml { class Element; class Event; }
namespace juceRmlUi { class Menu; class RmlComponent; }

namespace emu88Player
{
	class HardwareLcd;
	class SettingsWindow;
	class AboutWindow;
	class KeyboardWindow;

	// One line of the keyboard help: the key or key pair, and the panel legend.
	struct KeyboardShortcut { std::string keys, label; };
	// One panel area, in the order the front panel prints it.
	struct KeyboardShortcutGroup { std::string heading; std::vector<KeyboardShortcut> rows; };
	using KeyboardShortcutGroups = std::vector<KeyboardShortcutGroup>;
	class TitleBarButton;
	class PlaylistDropTarget;
	class PlaylistRowDrag;

	class Editor final : public juce::AudioProcessorEditor,
	                     private juce::Timer,
	                     public juceRmlUi::DataProvider,
	                     public juce::StandaloneOptionsMenuHandler
	{
	public:
		explicit Editor(Processor& _processor);
		~Editor() override;
		void resized() override;
		void parentHierarchyChanged() override;

		const char* getResourceByFilename(const std::string& _name, uint32_t& _dataSize) override;
		std::vector<std::string> getAllFilenames() override;
		void showStandaloneOptionsMenu() override;

	private:
		friend class SettingsWindow;
		friend class AboutWindow;
		friend class KeyboardWindow;

		struct Skin
		{
			std::string displayName;
			std::string filename;
			std::string folder;

			bool operator==(const Skin& _other) const
			{
				return filename == _other.filename && folder == _other.folder;
			}
		};

		void loadSkin(const Skin& _skin);
		Skin readSkinFromConfig() const;
		void writeSkinToConfig(const Skin& _skin) const;
		std::vector<Skin> findSkins() const;
		std::string skinFolder() const;
		void exportEmbeddedSkin();
		void createRmlUi();
		void destroyRmlUi();
		void wirePanel();
		void chooseMidiFiles();
		void addMidiFiles(const std::vector<std::string>& _files);
		void attachRecordButton();
		void updateRecordButton();
		void toggleWavRecording();
		void saveRecording(const juce::File& _recording);
		void refreshPlaylist();
		void updatePlayerVisuals(const MidiPlayer::Status& _status);

		void openContextMenu(Rml::Event& _event);
		void openPlaylistContextMenu(Rml::Event& _event);
		void openDeviceMenu(Rml::Event& _event);
		void selectDeviceModel(emu88Lib::DeviceModel _model, bool _restart = false);
		void updateDeviceSkin(emu88Lib::DeviceModel _model);
		void runContextMenu(const Rml::Element* _parent, float _x, float _y);
		void showAudioSettings();
		void showMidiSettings();
		void setGuiScale(int _percent);
		void adoptStandaloneSettings();
		void populateAudioMidiSettings();
		void persistAudioMidiSettings();
		void reopenSettings(const char* _pageId, const char* _buttonId);
		void showSettings(bool _show);
		void showAbout();
		void showKeyboard();
		KeyboardShortcutGroups buildKeyboardGroups();
		void initialiseSettings(const char* _pageId, const char* _buttonId);
		void selectSettingsPage(const char* _pageId, const char* _buttonId);
		void populateSkinSettings();
		void showStartupNotices();
		void showMissingRomNotice() const;

		void timerCallback() override;
		void setPointerButton(uint32_t _button, bool _pressed);
		void setKeyboardButton(uint32_t _button, bool _pressed);
		void sendButtons();
		void refreshButtonElements(emu88Lib::DeviceModel _model);
		void refreshLedElements(emu88Lib::DeviceModel _model);
		void updateButtonVisuals();
		void updateLeds(uint8_t _leds);

		Processor& m_processor;
		juceRmlUi::RmlInterfaces m_interfaces;
		std::unique_ptr<juceRmlUi::RmlComponent> m_rml;
		baseLib::EventListener<juceRmlUi::RmlComponent*> m_onRmlFocusLost;
		std::shared_ptr<juceRmlUi::Menu> m_contextMenu;
		std::unique_ptr<SettingsWindow> m_settingsWindow;
		std::unique_ptr<AboutWindow> m_aboutWindow;
		std::unique_ptr<KeyboardWindow> m_keyboardWindow;
		std::unique_ptr<HardwareLcd> m_lcd;
		std::unique_ptr<PlaylistDropTarget> m_playlistDropTarget;
		std::vector<std::unique_ptr<PlaylistRowDrag>> m_playlistRows;
		std::unique_ptr<juce::FileChooser> m_playlistChooser;
		std::unique_ptr<juce::FileChooser> m_recordingChooser;
		std::unique_ptr<TitleBarButton> m_recordButton;
		Skin m_skin;
		std::map<std::string, std::vector<char>> m_fileCache;
		Rml::Element* m_settingsRoot = nullptr;
		Rml::Element* m_playlistEntries = nullptr;
		Rml::Element* m_playerPlayGraphic = nullptr;
		Rml::Element* m_playerPauseGraphic = nullptr;
		std::array<Rml::Element*, 32> m_buttonElements{};
		std::array<Rml::Element*, 8> m_leds{};
		float m_valueKnobDownX = 0.0f;
		float m_valueKnobDownY = 0.0f;
		uint32_t m_pointerButtons = 0;
		uint32_t m_keyboardButtons = 0;
		uint32_t m_sentButtons = 0;
		uint64_t m_displayRevision = 0;
		uint64_t m_playlistRevision = ~uint64_t{0};
		uint64_t m_playerStatusRevision = ~uint64_t{0};
		juce::ComponentBoundsConstrainer m_sizeConstrainer;
		bool m_settingGuiScale = false;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
		JUCE_DECLARE_WEAK_REFERENCEABLE(Editor)
	};
}

#include "Emu88Editor.h"
#include "Emu88EditorBindings.h"
#include "Emu88EditorLcd.h"
#include "Emu88EditorPlaylist.h"
#include "Emu88EditorWindows.h"
#include "88lib/romloader.h"
#include "juceRmlUi/rmlMenu.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceUiLib/messageBox.h"
#include "juceUiLib/legalDisclaimer.h"
#include "RmlUi/Core/Context.h"

#include <algorithm>
#include <cmath>

namespace emu88Player
{
	using namespace editor;

	Editor::Editor(Processor& _processor)
		: juce::AudioProcessorEditor(_processor), m_processor(_processor), m_interfaces(*this)
	{
		adoptStandaloneSettings();
		m_sizeConstrainer.setMinimumSize(g_defaultWidth / 2, g_defaultHeight / 2);
		m_sizeConstrainer.setMaximumSize(g_defaultWidth * 3, g_defaultHeight * 3);
		m_sizeConstrainer.setFixedAspectRatio(static_cast<double>(g_defaultWidth) / g_defaultHeight);
		setResizable(true, true);
		setConstrainer(&m_sizeConstrainer);

		loadSkin(readSkinFromConfig());
		setGuiScale(juce::jlimit(50, 300, m_processor.config().getIntValue("scale", 100)));
		startTimerHz(30);

		const juce::WeakReference<Editor> safeThis(this);
		juce::MessageManager::callAsync([safeThis]
		{
			if(auto* editor = safeThis.get())
				editor->showStartupNotices();
		});
	}

	Editor::~Editor()
	{
		stopTimer();
		m_pointerButtons = 0;
		m_keyboardButtons = 0;
		sendButtons();
		m_settingsRoot = nullptr;
		m_settingsWindow.reset();
		destroyRmlUi();
	}

	void Editor::createRmlUi()
	{
		juceRmlUi::RmlComponentConfig config;
		config.refreshRateLimitHz = 30;
		config.includeDefaultTemplates = false;
		const auto software = m_processor.config().getIntValue("forceSoftwareRenderer", -1);
		if(software >= 0)
			config.forceSoftwareRenderer = software > 0 ? juceRmlUi::SoftwareRendererMode::ForceOn :
			                                             juceRmlUi::SoftwareRendererMode::ForceOff;
		m_rml = std::make_unique<juceRmlUi::RmlComponent>(
			m_interfaces, *this, m_skin.filename, 1.0f,
			[](juceRmlUi::RmlComponent&, Rml::Context&) {},
			[](juceRmlUi::RmlComponent&, Rml::Context&) {}, config);
		addAndMakeVisible(*m_rml);
		m_rml->setBounds(getLocalBounds());
		// A key or mouse button held while the panel loses focus would never get its release delivered
		// here, and the firmware would keep the switch pressed - a stuck PART arrow turns every later
		// arrow press into the ALL-mode chord. Let go of everything when the focus goes.
		m_onRmlFocusLost.set(m_rml->evFocusLost, [this](juceRmlUi::RmlComponent*)
		{
			m_pointerButtons = 0;
			m_keyboardButtons = 0;
			sendButtons();
			updateButtonVisuals();
		});
		wirePanel();
		{
			juceRmlUi::RmlInterfaces::ScopedAccess access(*m_rml);
			m_rml->enableDebugger(m_processor.config().getBoolValue("enableRmlUiDebugger", false));
		}
	}

	void Editor::destroyRmlUi()
	{
		m_contextMenu.reset();
		m_playlistRows.clear();
		m_playlistDropTarget.reset();
		m_playlistEntries = nullptr;
		m_playerPlayGraphic = nullptr;
		m_playerPauseGraphic = nullptr;
		m_lcd.reset();
		m_buttonElements.fill(nullptr);
		m_leds.fill(nullptr);
		m_onRmlFocusLost.reset();
		m_rml.reset();
		m_fileCache.clear();
	}

	void Editor::parentHierarchyChanged()
	{
		juce::AudioProcessorEditor::parentHierarchyChanged();
		attachRecordButton();
	}

	void Editor::resized()
	{
		if(m_rml)
			m_rml->setBounds(getLocalBounds());
		if(m_settingGuiScale || getWidth() <= 0)
			return;
		const auto scale = juce::roundToInt(100.0 * static_cast<double>(getWidth()) / g_defaultWidth);
		m_processor.config().setValue("scale", scale);
		m_processor.config().saveIfNeeded();
	}

	void Editor::openContextMenu(Rml::Event& _event)
	{
		const auto position = juceRmlUi::helper::getMousePos(_event);
		runContextMenu(_event.GetTargetElement(), position.x, position.y);
	}

	void Editor::openDeviceMenu(Rml::Event& _event)
	{
		m_contextMenu = std::make_shared<juceRmlUi::Menu>();
		// A board whose ROM set is incomplete stays visible but greyed out: it
		// says what the emulator can run rather than silently hiding boards the
		// user may be about to add ROMs for.
		const auto inventory = emu88Lib::RomLoader::rescan();
		for(uint32_t value = 0; value < emu88Lib::deviceModelCount(); ++value)
		{
			const auto model = static_cast<emu88Lib::DeviceModel>(value);
			const auto available = inventory.isComplete(emu88Lib::RomLoader::toRomDevice(model));
			m_contextMenu->addEntry(emu88Lib::getDeviceProfile(model).displayName, available,
			                        model == m_processor.deviceModel(), [this, model]
			{
				selectDeviceModel(model);
			});
		}
		auto* target = _event.GetTargetElement();
		m_contextMenu->open(target, target->GetAbsoluteOffset(Rml::BoxArea::Border), 16);
	}

	void Editor::selectDeviceModel(const emu88Lib::DeviceModel _model, const bool _restart)
	{
		if(!_restart && _model == m_processor.deviceModel() && m_processor.hasValidRom())
			return;

		m_pointerButtons = 0;
		m_keyboardButtons = 0;
		sendButtons();
		updateButtonVisuals();
		const bool valid = _restart ? m_processor.restartDevice() : m_processor.setDeviceModel(_model);
		m_sentButtons = 0;
		m_displayRevision = 0;
		updateDeviceSkin(_model);
		if(m_lcd)
			m_lcd->reset(_model);
		updateLeds(0);
		if(!valid)
			showMissingRomNotice();
	}

	void Editor::updateDeviceSkin(const emu88Lib::DeviceModel _model)
	{
		if(!m_rml)
			return;
		auto* document = m_rml->getDocument();
		const char* panel = "sc88pro_panel.png";
		switch(_model)
		{
		case emu88Lib::DeviceModel::Sc88: panel = "sc88_panel.png"; break;
		case emu88Lib::DeviceModel::Sc88VL: panel = "sc88vl_panel.png"; break;
		case emu88Lib::DeviceModel::Sc88Pro: panel = "sc88pro_panel.png"; break;
		case emu88Lib::DeviceModel::Sc8850: panel = "sc8850_panel.png"; break;
		case emu88Lib::DeviceModel::Sc55Mk2: panel = "sc55_panel.png"; break;
		}
		if(auto* image = document->GetElementById("hardwarePanel"))
			image->SetAttribute("src", panel);
		if(auto* root = document->GetElementById("Root"))
		{
			root->SetClass("modelSc88", _model == emu88Lib::DeviceModel::Sc88);
			root->SetClass("modelSc88VL", _model == emu88Lib::DeviceModel::Sc88VL);
			root->SetClass("modelSc88Pro", _model == emu88Lib::DeviceModel::Sc88Pro);
			root->SetClass("modelSc8850", _model == emu88Lib::DeviceModel::Sc8850);
			root->SetClass("modelSc55Mk2", _model == emu88Lib::DeviceModel::Sc55Mk2);
		}
		if(auto* mapOrEq = document->GetElementById("btSc88Map"))
			mapOrEq->SetAttribute("title", _model == emu88Lib::DeviceModel::Sc88Pro
				? "SC-88 MAP (2)" : "EQ (2)");
		refreshButtonElements(_model);
		// Drop the previous model's lit LEDs before the element list moves on.
		updateLeds(0);
		refreshLedElements(_model);

		// Attribute and class changes dirty the RML document, but this standalone's
		// throttled renderer still needs to be woken explicitly for a live model switch.
		m_rml->enqueueUpdate();
	}

	void Editor::showStandaloneOptionsMenu()
	{
		if(!m_rml)
			return;

		const juceRmlUi::RmlInterfaces::ScopedAccess access(*m_rml);
		runContextMenu(m_rml->getDocument(), 4.0f, 4.0f);
	}

	void Editor::runContextMenu(const Rml::Element* _parent, const float _x, const float _y)
	{
		if(!_parent)
			return;

		const auto currentScale = m_processor.config().getIntValue("scale", 100);
		m_contextMenu = std::make_shared<juceRmlUi::Menu>();
		m_contextMenu->addEntry("Send GM Reset", [this]
		{
			m_processor.sendGmReset();
		});
		m_contextMenu->addEntry("Send GS Reset", [this]
		{
			m_processor.sendGsReset();
		});
		m_contextMenu->addEntry("Send All Notes Off", [this]
		{
			m_processor.sendAllNotesOff();
		});
		m_contextMenu->addEntry("Restart Device", [this]
		{
			const juce::WeakReference<Editor> safeThis(this);
			juce::MessageManager::callAsync([safeThis]
			{
				if(auto* editor = safeThis.get())
					editor->selectDeviceModel(editor->m_processor.deviceModel(), true);
			});
		});
		m_contextMenu->addSeparator();
		juceRmlUi::Menu scaleMenu;
		for(const auto scale : g_guiScales)
			scaleMenu.addEntry(std::to_string(scale) + '%', scale == currentScale, [this, scale]
			{
				setGuiScale(scale);
			});
		// GUI Scale, Settings and the two device-specific pages are one group,
		// with the audio/MIDI pages under the general one.
		m_contextMenu->addSubMenu("GUI Scale", std::move(scaleMenu));
		m_contextMenu->addEntry("Settings...", [this]
		{
			const juce::WeakReference<Editor> safeThis(this);
			juce::MessageManager::callAsync([safeThis]
			{
				if(auto* editor = safeThis.get())
					editor->showSettings(true);
			});
		});
		m_contextMenu->addEntry("Audio Settings...", [this]
		{
			const juce::WeakReference<Editor> safeThis(this);
			juce::MessageManager::callAsync([safeThis]
			{
				if(auto* editor = safeThis.get())
					editor->showAudioSettings();
			});
		});
		m_contextMenu->addEntry("MIDI Settings...", [this]
		{
			const juce::WeakReference<Editor> safeThis(this);
			juce::MessageManager::callAsync([safeThis]
			{
				if(auto* editor = safeThis.get())
					editor->showMidiSettings();
			});
		});
		m_contextMenu->addSeparator();
		// The build, as a heading rather than a command - not clickable.
		m_contextMenu->addEntry(m_processor.getName().toStdString() + ' ' + g_pluginVersionString,
			false, false, [] {});
		m_contextMenu->addEntry("Donate...", []
		{
			juce::URL(g_donateUrl).launchInDefaultBrowser();
		});
		m_contextMenu->addEntry("Keyboard shortcuts...", [this]
		{
			const juce::WeakReference<Editor> safeThis(this);
			juce::MessageManager::callAsync([safeThis]
			{
				if(auto* editor = safeThis.get())
					editor->showKeyboard();
			});
		});
		m_contextMenu->addEntry("About...", [this]
		{
			const juce::WeakReference<Editor> safeThis(this);
			juce::MessageManager::callAsync([safeThis]
			{
				if(auto* editor = safeThis.get())
					editor->showAbout();
			});
		});
		m_contextMenu->open(_parent, Rml::Vector2f(_x, _y), 16);
	}

	void Editor::showStartupNotices()
	{
		if(m_processor.config().getBoolValue("disclaimerSeen", false))
		{
			showMissingRomNotice();
			return;
		}

		const juce::WeakReference<Editor> safeThis(this);
		genericUI::showLegalDisclaimer("88emuPlayer", [safeThis]
		{
			if(auto* editor = safeThis.get())
			{
				editor->m_processor.config().setValue("disclaimerSeen", true);
				editor->m_processor.config().saveIfNeeded();
				editor->showMissingRomNotice();
			}
		});
	}

	void Editor::showMissingRomNotice() const
	{
		if(m_processor.hasValidRom())
			return;
		const auto model = m_processor.deviceModel();
		const auto device = emu88Lib::RomLoader::toRomDevice(model);

		const auto inventory = emu88Lib::RomLoader::scan();
		// Pro has several compatible sources for each required component.
		// Explain those alternatives instead of asking only for native chips.
		std::ostringstream files;
		if(model == emu88Lib::DeviceModel::Sc88Pro)
		{
			if(!inventory.has(device, emu88Lib::RomSlot::Control))
				files << "Control ROM missing: use an SC-88Pro or VE-GSPro control ROM (1 MiB). "
				         "Supported 2/4 MiB repeated Pro dumps are also accepted.\n\n";
			if(!inventory.has(device, emu88Lib::RomSlot::Wave, 0) ||
			   !inventory.has(device, emu88Lib::RomSlot::Wave, 1) ||
			   !inventory.has(device, emu88Lib::RomSlot::Wave, 2))
				files << "Wave data missing: use one of these compatible sources:\n"
				         "- SC-88Pro / VE-GSPro wave ROMs A+B+C (8+8+4 MiB)\n"
				         "- Decoded SC-8850 waves (32 MiB) or SC-8820 waves (16+8 MiB)\n"
				         "- A Sound Canvas VA .dll or .dylib containing the SC-8820 waves, "
				         "such as SCCore.dll or SCCore00.dylib\n\n"
				         "DLL/dylib files supply wave data only; a separate control ROM is required. "
				         "No manual extraction or conversion is needed.\n";
		}
		else
		{
			for(const auto* entry : inventory.missing(device))
				files << emu88Lib::describe(*entry) << "  -  " << (entry->size >> 10) << " KiB  -  "
				      << entry->hash.toString().substr(0, 8) << '\n';
		}

		const auto message = std::string("The complete ") + emu88Lib::getDeviceProfile(model).displayName +
			" ROM set was not found.\n\nCopy the missing images to\n" + m_processor.romFolder() +
			"\n(subfolders are searched too, and the files may be named anything)\n\n" + files.str() +
			"\nThe target folder will be opened after you click OK. Then select the device again or restart the application.";
		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
			"88emuPlayer - Device initialization failed", message, [folder = m_processor.romFolder()]
			{
				const auto path = juce::File(folder);
				(void)path.createDirectory();
				path.revealToUser();
			});
	}

	void Editor::timerCallback()
	{
		if(!m_rml)
			return;
		auto* hardware = m_processor.hardware();
		const auto playlistRevision = m_processor.midiPlayer().playlistRevision();
		const auto playerStatus = m_processor.midiPlayer().status();
		const auto snapshot = hardware ? hardware->displaySnapshot()
		                               : emu88Lib::HardwareDevice::DisplaySnapshot{};
		const bool displayChanged = snapshot.revision != 0 && snapshot.revision != m_displayRevision;
		const bool playlistChanged = playlistRevision != m_playlistRevision;
		const bool playerChanged = playerStatus.revision != m_playerStatusRevision;
		if(!displayChanged && !playlistChanged && !playerChanged)
			return;
		juceRmlUi::RmlInterfaces::ScopedAccess access(*m_rml);
		if(displayChanged)
		{
			m_displayRevision = snapshot.revision;
			if(m_lcd)
				m_lcd->setSnapshot(snapshot);
			updateLeds(snapshot.leds);
		}
		if(playlistChanged)
			refreshPlaylist();
		else if(playerChanged)
			updatePlayerVisuals(playerStatus);
	}
}

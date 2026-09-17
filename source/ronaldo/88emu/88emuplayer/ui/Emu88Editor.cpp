#include "88emuplayer/ui/Emu88Editor.h"
#include "88emuplayer/ui/Emu88EditorBindings.h"
#include "88emuplayer/ui/Emu88EditorLcd.h"
#include "88emuplayer/ui/Emu88EditorPlaylist.h"
#include "88emuplayer/ui/Emu88EditorWindows.h"
#include "88lib/rom/romloader.h"
#include "juceRmlUi/rmlMenu.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceUiLib/messageBox.h"
#include "juceUiLib/legalDisclaimer.h"
#include "RmlUi/Core/Context.h"

#include <algorithm>
#include <cmath>
#include <sstream>

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
		m_powerKeyDown = false;
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
		// Playlist entries are file names, in whatever script the user's files happen to use.
		config.systemFallbackFonts = true;
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
			m_powerKeyDown = false;
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
		m_lcd2.reset();
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
		const auto inventory = emu88Lib::RomLoader::rescan();
		for(const auto model : emu88Lib::g_deviceMenuOrder)
		{
			// A hidden device stays in the menu only while it is the one running.
			if(!emu88Lib::isDeviceListed(model) && model != m_processor.deviceModel())
				continue;
			const auto available = inventory.isComplete(emu88Lib::RomLoader::toRomDevice(model));
			const auto label = std::string(emu88Lib::getDeviceProfile(model).displayName) +
				(available ? "" : " (ROMs missing)");
			m_contextMenu->addEntry(label, true, model == m_processor.deviceModel(), [this, model, available]
			{
				if(available)
					selectDeviceModel(model);
				else
				{
					const juce::WeakReference<Editor> safeThis(this);
					juce::MessageManager::callAsync([safeThis, model]
					{
						if(auto* editor = safeThis.get())
							editor->showMissingRomNotice(model);
					});
				}
			}, available ? "" : "romMissing");
		}

		auto* target = _event.GetTargetElement();
		m_contextMenu->openPopupWindow(target, target->GetAbsoluteOffset(Rml::BoxArea::Border),
			target->GetBox().GetSize(Rml::BoxArea::Border));
	}

	void Editor::selectDeviceModel(const emu88Lib::DeviceModel _model, const bool _restart, const bool _powerOn)
	{
		if(!_restart && _model == m_processor.deviceModel() && m_processor.hasValidRom())
			return;

		if(!_powerOn)
		{
			m_pointerButtons = m_keyboardButtons = 0;
			m_powerKeyDown = false;
			sendButtons();
		}
		updateButtonVisuals();
		const auto held = m_pointerButtons | m_keyboardButtons;
		const bool valid = _powerOn
			? m_processor.setPower(true, panelButtonsForDevice(_model, held))
			: (_restart ? m_processor.restartDevice() : m_processor.setDeviceModel(_model));
		m_sentButtons = _powerOn ? held : 0;
		m_displayRevision = 0;
		updateDeviceSkin(_model);
		updatePowerVisuals();
		if(m_lcd)
			m_lcd->reset(_model);
		if(m_lcd2)
			m_lcd2->reset(_model, 1);
		updateLeds(0);
		// The analogue output setting names the circuit Auto picks for the current board, and the
		// MIDI page dims the part groups it lacks.
		if(m_settingsRoot)
			for(const auto& [page, button] : {std::pair{"pageSettingsAudio", "btSettingsAudio"},
			                                  std::pair{"pageSettingsMidi", "btSettingsMidi"}})
				if(auto* element = m_settingsRoot->GetElementById(page); element && element->IsVisible())
					reopenSettings(page, button);
		if(!valid)
			showMissingRomNotice(m_processor.deviceModel());
		else if(!_powerOn)
			showRomWarnings();
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
		case emu88Lib::DeviceModel::Sc55Mk2:
		case emu88Lib::DeviceModel::Sc155Mk2: panel = "sc55mk2_panel.png"; break;
		case emu88Lib::DeviceModel::Sc55Mk1:
		case emu88Lib::DeviceModel::Sc155: panel = "sc55_panel.png"; break;
		// Boards without a front panel: artwork with only the player, device selector and volume.
		case emu88Lib::DeviceModel::Cm32p: panel = "cm32p_panel.png"; break;
		case emu88Lib::DeviceModel::Cm32l: panel = "cm32l_panel.png"; break;
		case emu88Lib::DeviceModel::Cm64: panel = "cm64_panel.png"; break;
		case emu88Lib::DeviceModel::Sc8820: panel = "sc8820_panel.png"; break;
		case emu88Lib::DeviceModel::Xpgs:
		case emu88Lib::DeviceModel::VeGsPro: panel = "sc88exp_panel.png"; break;
		case emu88Lib::DeviceModel::Sc55St:
		case emu88Lib::DeviceModel::Cm300:
		case emu88Lib::DeviceModel::Scc1a:
		case emu88Lib::DeviceModel::Scb55:
		case emu88Lib::DeviceModel::Rlp3237: panel = "sc55pc_panel.png"; break;
		}
		// The boards whose switches the panel filter drops; their artwork has none to show.
		const bool noPanel = panelButtonsForDevice(_model, ~uint32_t{0}) == 0;
		if(auto* image = document->GetElementById("hardwarePanel"))
			image->SetAttribute("src", panel);
		// The grey panels get darker transport faces.
		const bool darkTransport = emu88Lib::isCmModel(_model) || _model == emu88Lib::DeviceModel::Sc8850 ||
		                           _model == emu88Lib::DeviceModel::Sc8820;
		for(const auto& [id, name] : {std::pair{"playerPlayGraphic", "player_play"}, std::pair{"playerPauseGraphic", "player_pause"},
		                              std::pair{"playerStopGraphic", "player_stop"}})
			if(auto* graphic = document->GetElementById(id))
				graphic->SetAttribute("src", std::string(name) + (darkTransport ? "_dark.svg" : ".svg"));
		if(auto* root = document->GetElementById("Root"))
		{
			root->SetClass("modelSc88", _model == emu88Lib::DeviceModel::Sc88);
			root->SetClass("modelSc88VL", _model == emu88Lib::DeviceModel::Sc88VL);
			root->SetClass("modelSc88Pro", _model == emu88Lib::DeviceModel::Sc88Pro);
			root->SetClass("modelSc8850", _model == emu88Lib::DeviceModel::Sc8850);
			root->SetClass("modelSc55", emu88Lib::isSc55Model(_model) && !noPanel);
			root->SetClass("modelCm", emu88Lib::isCmModel(_model));
			// The CM bezels differ in their windows: one 16x2 on the CM-32P, one 20x1 on the
			// CM-32L, and both on the CM-64.
			root->SetClass("modelCm32l", _model == emu88Lib::DeviceModel::Cm32l);
			root->SetClass("modelCm64", _model == emu88Lib::DeviceModel::Cm64);
			root->SetClass("modelSc8820", _model == emu88Lib::DeviceModel::Sc8820);
			root->SetClass("modelNoPanel", noPanel);
			root->SetClass("modelNoDisplay", !emu88Lib::deviceHasLcd(_model));
		}
		if(auto* mapOrEq = document->GetElementById("btSc88Map"))
			mapOrEq->SetAttribute("title", _model == emu88Lib::DeviceModel::Sc88Pro ? "SC-88 MAP (2)" : "EQ (2)");
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
		m_contextMenu->openPopupWindow(_parent, Rml::Vector2f(_x, _y));
	}

	void Editor::showStartupNotices()
	{
		if(m_processor.config().getBoolValue("disclaimerSeen", false))
		{
			showRomNotices();
			return;
		}

		const juce::WeakReference<Editor> safeThis(this);
		genericUI::showLegalDisclaimer("88emuPlayer", [safeThis]
		{
			if(auto* editor = safeThis.get())
			{
				editor->m_processor.config().setValue("disclaimerSeen", true);
				editor->m_processor.config().saveIfNeeded();
				editor->showRomNotices();
			}
		});
	}

	void Editor::showRomNotices()
	{
		if(m_processor.hasValidRom())
			showRomWarnings();
		else
			showMissingRomNotice(m_processor.deviceModel());
	}

	void Editor::showMissingRomNotice(const emu88Lib::DeviceModel _model)
	{
		const auto inventory = emu88Lib::RomLoader::rescan();
		const auto device = emu88Lib::RomLoader::toRomDevice(_model);
		const auto message = std::string("ROM folder: ") + m_processor.romFolder() +
			"\nSubfolders are searched too. Add the missing files, then select the device again.\n\n" +
			inventory.describeRequirements(device);
		m_romWindow = std::make_unique<RomRequirementsWindow>(*this,
			emu88Lib::getDeviceProfile(_model).displayName, message, m_processor.romFolder());
		m_romWindow->setVisible(true);
	}

	void Editor::showRomWarnings() const
	{
		const auto device = emu88Lib::RomLoader::toRomDevice(m_processor.deviceModel());
		const auto warnings = emu88Lib::RomLoader::scan().warnings(device,
			m_processor.config().getBoolValue("warnRomHashMismatch", true));
		if(warnings.empty()) return;
		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
			"88emuPlayer - ROM warning", warnings, [] {});
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
				m_lcd->setSnapshot(snapshot.screens[0]);
			if(m_lcd2)
				m_lcd2->setSnapshot(snapshot.screens[1]);
			updateLeds(snapshot.leds);
		}
		if(playlistChanged)
			refreshPlaylist();
		else if(playerChanged)
			updatePlayerVisuals(playerStatus);
	}
}

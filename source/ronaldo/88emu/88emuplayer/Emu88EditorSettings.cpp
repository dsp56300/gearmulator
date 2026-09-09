#include "Emu88Editor.h"
#include "Emu88EditorBindings.h"
#include "Emu88AudioSettings.h"
#include "Emu88PortMidiBridge.h"
#include "Emu88EditorWindows.h"
#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceUiLib/messageBox.h"
#include "RmlUi/Core/StringUtilities.h"
#include "juce_audio_utils/juce_audio_utils.h"
#include "juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace emu88Player
{
	using namespace editor;

	void Editor::setGuiScale(const int _percent)
	{
		m_settingGuiScale = true;
		setSize(g_defaultWidth * _percent / 100, g_defaultHeight * _percent / 100);
		m_settingGuiScale = false;
		m_processor.config().setValue("scale", _percent);
		m_processor.config().saveIfNeeded();
	}

	void Editor::adoptStandaloneSettings()
	{
		auto* holder = juce::StandalonePluginHolder::getInstance();
		if(!holder)
			return;

		auto* active = dynamic_cast<juce::PropertiesFile*>(holder->settings.get());
		if(active && active->getFile() == m_processor.config().getFile())
		{
			m_processor.useConfig(*active);
			return;
		}

		auto config = m_processor.takeConfigOwnership();
		if(!config)
			return;

		auto* adoptedConfig = config.get();
		holder->settings.setOwned(config.release());
		m_processor.useConfig(*adoptedConfig);

		// The stock standalone wrapper opens its audio devices before creating the
		// editor. Apply the consolidated Documents state once after taking over its
		// settings so a stale legacy JUCE settings file cannot win at startup.
		if(auto savedState = adoptedConfig->getXmlValue("audioSetup"))
		{
			const auto error = holder->deviceManager.initialise(
				0, m_processor.getMainBusNumOutputChannels(), savedState.get(), true);
			if(error.isNotEmpty())
				juce::Logger::writeToLog("Unable to restore 88emuPlayer audio/MIDI settings: " + error);
		}
	}

	void Editor::persistAudioMidiSettings()
	{
		if(auto* holder = juce::StandalonePluginHolder::getInstance())
		{
			auto state = holder->deviceManager.createStateXml();
			m_processor.config().setValue("audioSetup", state.get());
			m_processor.config().saveIfNeeded();
		}
	}

	void Editor::reopenSettings(const char* _pageId, const char* _buttonId)
	{
		const juce::WeakReference<Editor> safeThis(this);
		juce::MessageManager::callAsync([safeThis, _pageId, _buttonId]
		{
			auto* editor = safeThis.get();
			if(!editor)
				return;
			if(!editor->m_settingsWindow)
			{
				editor->showSettings(true);
				editor->selectSettingsPage(_pageId, _buttonId);
				return;
			}
			const auto position = editor->m_settingsWindow->getPosition();
			editor->m_settingsRoot = nullptr;
			editor->m_settingsWindow->reloadContent();
			editor->m_settingsWindow->setTopLeftPosition(position);
			editor->initialiseSettings(_pageId, _buttonId);
		});
	}

	void Editor::showAudioSettings()
	{
		showSettings(true);
		selectSettingsPage("pageSettingsAudio", "btSettingsAudio");
	}

	void Editor::showMidiSettings()
	{
		showSettings(true);
		selectSettingsPage("pageSettingsMidi", "btSettingsMidi");
	}

	void Editor::selectSettingsPage(const char* _pageId, const char* _buttonId)
	{
		if(!m_settingsRoot)
			return;
		for(const auto* page : {"pageSettingsSkin", "pageSettingsGui", "pageSettingsAudio", "pageSettingsMidi",
		                       "pageSettingsDeveloper"})
			if(auto* element = m_settingsRoot->GetElementById(page))
			{
				if(std::string(page) == _pageId)
					element->RemoveProperty(Rml::PropertyId::Display);
				else
					element->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
			}
		for(const auto* button : {"btSettingsSkin", "btSettingsGui", "btSettingsAudio", "btSettingsMidi",
		                          "btSettingsDeveloper"})
			if(auto* element = m_settingsRoot->GetElementById(button))
				element->SetPseudoClass("checked", std::string(button) == _buttonId);
	}

	void Editor::populateAudioMidiSettings()
	{
		auto* holder = juce::StandalonePluginHolder::getInstance();
		if(!holder || !m_settingsRoot)
			return;
		auto& manager = holder->deviceManager;

		const auto combo = [this](const char* _id)
		{
			return dynamic_cast<juceRmlUi::ElemComboBox*>(m_settingsRoot->GetElementById(_id));
		};
		const auto initialiseCombo = [](juceRmlUi::ElemComboBox* _combo,
		                                const std::vector<Rml::String>& _labels, const int _selected)
		{
			if(!_combo)
				return;
			_combo->setOptions(_labels);
			if(!_labels.empty())
				_combo->setSelectedIndex(static_cast<size_t>(std::clamp(_selected, 0,
					static_cast<int>(_labels.size()) - 1)), false);
		};

		std::vector<juce::String> typeNames;
		std::vector<Rml::String> typeLabels;
		int selectedType = 0;
		const auto& types = manager.getAvailableDeviceTypes();
		for(int i = 0; i < types.size(); ++i)
		{
			typeNames.push_back(types[i]->getTypeName());
			typeLabels.push_back(typeNames.back().toStdString());
			if(typeNames.back() == manager.getCurrentAudioDeviceType())
				selectedType = i;
		}
		if(auto* typeCombo = combo("audioDeviceType"))
		{
			initialiseCombo(typeCombo, typeLabels, selectedType);
			juceRmlUi::EventListener::Add(typeCombo, Rml::EventId::Change, [this, typeCombo, typeNames](Rml::Event&)
			{
				const auto index = typeCombo->getSelectedIndex();
				if(index < 0 || index >= static_cast<int>(typeNames.size()))
					return;
				if(auto* currentHolder = juce::StandalonePluginHolder::getInstance())
				{
					const auto error = selectAudioDeviceType(currentHolder->deviceManager,
						typeNames[static_cast<size_t>(index)], m_processor.getMainBusNumOutputChannels());
					if(error.isNotEmpty())
						genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
							"88emuPlayer", error.toStdString());
					persistAudioMidiSettings();
					reopenSettings("pageSettingsAudio", "btSettingsAudio");
				}
			});
		}

		juce::AudioIODeviceType* currentType = nullptr;
		for(auto* type : types)
			if(type->getTypeName() == manager.getCurrentAudioDeviceType())
			{
				currentType = type;
				break;
			}

		const auto setup = manager.getAudioDeviceSetup();
		juce::StringArray outputNames;
		if(currentType)
		{
			currentType->scanForDevices();
			outputNames = currentType->getDeviceNames(false);
		}
		std::vector<Rml::String> outputLabels{"<none>"};
		int selectedOutput = 0;
		for(int i = 0; i < outputNames.size(); ++i)
		{
			outputLabels.push_back(outputNames[i].toStdString());
			if(outputNames[i] == setup.outputDeviceName)
				selectedOutput = i + 1;
		}
		if(auto* outputCombo = combo("audioOutputDevice"))
		{
			initialiseCombo(outputCombo, outputLabels, selectedOutput);
			juceRmlUi::EventListener::Add(outputCombo, Rml::EventId::Change, [this, outputCombo, outputNames](Rml::Event&)
			{
				const auto index = outputCombo->getSelectedIndex() - 1;
				if(index < -1 || index >= outputNames.size())
					return;
				if(auto* currentHolder = juce::StandalonePluginHolder::getInstance())
				{
					const auto error = selectAudioOutputDevice(currentHolder->deviceManager,
						index >= 0 ? outputNames[index] : juce::String{});
					if(error.isNotEmpty())
						genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
							"88emuPlayer", error.toStdString());
					persistAudioMidiSettings();
					reopenSettings("pageSettingsAudio", "btSettingsAudio");
				}
			});
		}

		if(auto* device = manager.getCurrentAudioDevice())
		{
			const auto rates = device->getAvailableSampleRates();
			std::vector<Rml::String> rateLabels;
			int selectedRate = 0;
			for(int i = 0; i < rates.size(); ++i)
			{
				rateLabels.push_back(juce::String(rates[i], rates[i] == std::floor(rates[i]) ? 0 : 1).toStdString() + " Hz");
				if(std::abs(rates[i] - device->getCurrentSampleRate()) < 0.5)
					selectedRate = i;
			}
			if(auto* rateCombo = combo("audioSampleRate"))
			{
				initialiseCombo(rateCombo, rateLabels, selectedRate);
				juceRmlUi::EventListener::Add(rateCombo, Rml::EventId::Change, [this, rateCombo, rates](Rml::Event&)
				{
					const auto index = rateCombo->getSelectedIndex();
					if(index < 0 || index >= rates.size())
						return;
					if(auto* currentHolder = juce::StandalonePluginHolder::getInstance())
					{
						auto newSetup = currentHolder->deviceManager.getAudioDeviceSetup();
						newSetup.sampleRate = rates[index];
						const auto error = currentHolder->deviceManager.setAudioDeviceSetup(newSetup, true);
						if(error.isNotEmpty())
							genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
								"88emuPlayer", error.toStdString());
						persistAudioMidiSettings();
						reopenSettings("pageSettingsAudio", "btSettingsAudio");
					}
				});
			}

			const auto sizes = device->getAvailableBufferSizes();
			std::vector<Rml::String> sizeLabels;
			int selectedSize = 0;
			for(int i = 0; i < sizes.size(); ++i)
			{
				sizeLabels.push_back(std::to_string(sizes[i]) + " samples");
				if(sizes[i] == device->getCurrentBufferSizeSamples())
					selectedSize = i;
			}
			if(auto* sizeCombo = combo("audioBufferSize"))
			{
				initialiseCombo(sizeCombo, sizeLabels, selectedSize);
				juceRmlUi::EventListener::Add(sizeCombo, Rml::EventId::Change, [this, sizeCombo, sizes](Rml::Event&)
				{
					const auto index = sizeCombo->getSelectedIndex();
					if(index < 0 || index >= sizes.size())
						return;
					if(auto* currentHolder = juce::StandalonePluginHolder::getInstance())
					{
						auto newSetup = currentHolder->deviceManager.getAudioDeviceSetup();
						newSetup.bufferSize = sizes[index];
						const auto error = currentHolder->deviceManager.setAudioDeviceSetup(newSetup, true);
						if(error.isNotEmpty())
							genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
								"88emuPlayer", error.toStdString());
						persistAudioMidiSettings();
						reopenSettings("pageSettingsAudio", "btSettingsAudio");
					}
				});
			}
		}

		// ASIO drivers own their sample rate and buffer size: the combos above
		// can only offer what the driver already exposes, and everything else
		// lives in the driver's own panel. JUCE's stock device selector has this
		// button, so a replicated panel needs it too or ASIO is half-usable.
		{
			auto* device = manager.getCurrentAudioDevice();
			const bool hasPanel = device && device->hasControlPanel();
			if(auto* row = m_settingsRoot->GetElementById("audioControlPanelRow"))
				juceRmlUi::helper::setVisible(row, hasPanel);
			if(auto* panelButton = m_settingsRoot->GetElementById("btAudioControlPanel"); panelButton && hasPanel)
			{
				juceRmlUi::EventListener::AddClick(panelButton, [this]
				{
					auto* currentHolder = juce::StandalonePluginHolder::getInstance();
					auto* currentDevice = currentHolder ? currentHolder->deviceManager.getCurrentAudioDevice() : nullptr;
					if(!currentDevice || !currentDevice->hasControlPanel())
						return;
					// Block interaction with the settings while a native driver panel
					// pumps messages, just as JUCE's stock device selector does.
					juce::Component modalWindow;
					modalWindow.addToDesktop(0);
					modalWindow.enterModalState();
					const auto changed = showAudioDeviceControlPanel(currentHolder->deviceManager);
					if(changed)
						persistAudioMidiSettings();
					reopenSettings("pageSettingsAudio", "btSettingsAudio");
				});
			}
		}

		auto midiOutputs = juce::MidiOutput::getAvailableDevices();
		for(int i = midiOutputs.size(); --i >= 0;)
			if(PortMidiBridge::isOwnVirtualPortName(midiOutputs[i].name))
				midiOutputs.remove(i);
		std::vector<Rml::String> midiOutputLabels{"<none>"};
		int selectedMidiOutput = 0;
		for(int i = 0; i < midiOutputs.size(); ++i)
		{
			midiOutputLabels.push_back(midiOutputs[i].name.toStdString());
			if(midiOutputs[i].identifier == manager.getDefaultMidiOutputIdentifier())
				selectedMidiOutput = i + 1;
		}
		if(auto* midiOutputCombo = combo("midiOutputDevice"))
		{
			initialiseCombo(midiOutputCombo, midiOutputLabels, selectedMidiOutput);
			juceRmlUi::EventListener::Add(midiOutputCombo, Rml::EventId::Change,
				[this, midiOutputCombo, midiOutputs](Rml::Event&)
				{
					const auto index = midiOutputCombo->getSelectedIndex() - 1;
					if(auto* currentHolder = juce::StandalonePluginHolder::getInstance())
					{
						currentHolder->deviceManager.setDefaultMidiOutputDevice(
							index >= 0 && index < midiOutputs.size() ? midiOutputs[index].identifier : juce::String{});
						persistAudioMidiSettings();
					}
				});
		}

		// PortMidi can only create virtual endpoints on CoreMIDI and ALSA, so on
		// Windows the switch would be a no-op. Say that instead of offering it.
		const auto virtualPorts = PortMidiBridge::virtualPortsSupported();
		if(auto* unsupported = m_settingsRoot->GetElementById("portMidiUnsupported"))
			juceRmlUi::helper::setVisible(unsupported, !virtualPorts);
		if(auto* portMidi = m_settingsRoot->GetElementById("btPortMidi"))
		{
			juceRmlUi::helper::setVisible(portMidi, virtualPorts);
			if(virtualPorts)
			{
				auto* button = juceRmlUi::helper::findChild(portMidi, "button");
				juceRmlUi::ElemButton::setChecked(button, m_processor.portMidiEnabled());
				juceRmlUi::EventListener::AddClick(portMidi, [this, button]
				{
					const auto enabled = !m_processor.portMidiEnabled();
					m_processor.setPortMidiEnabled(enabled);
					juceRmlUi::ElemButton::setChecked(button, enabled);
				});
			}
		}

		auto* inputTemplate = m_settingsRoot->GetElementById("midiInputEntry");
		if(inputTemplate)
		{
			auto* parent = inputTemplate->GetParentNode();
			auto midiInputs = juce::MidiInput::getAvailableDevices();
			for(int i = midiInputs.size(); --i >= 0;)
				if(PortMidiBridge::isOwnVirtualPortName(midiInputs[i].name))
					midiInputs.remove(i);
			if(auto* empty = m_settingsRoot->GetElementById("midiInputEmpty"))
				juceRmlUi::helper::setVisible(empty, midiInputs.isEmpty());
			for(int i = 0; i < midiInputs.size(); ++i)
			{
				const auto& device = midiInputs[i];
				auto entry = inputTemplate->Clone();
				entry->SetId("midiInput" + std::to_string(i));
				entry->RemoveProperty(Rml::PropertyId::Display);
				auto* button = juceRmlUi::helper::findChild(entry.get(), "button");
				auto* label = juceRmlUi::helper::findChild(entry.get(), "label");
				label->SetInnerRML(Rml::StringUtilities::EncodeRml(device.name.toStdString()));
				juceRmlUi::ElemButton::setChecked(button, manager.isMidiInputDeviceEnabled(device.identifier));
				juceRmlUi::EventListener::AddClick(entry.get(), [this, button, id = device.identifier]
				{
					if(auto* currentHolder = juce::StandalonePluginHolder::getInstance())
					{
						const auto enabled = !currentHolder->deviceManager.isMidiInputDeviceEnabled(id);
						currentHolder->deviceManager.setMidiInputDeviceEnabled(id, enabled);
						juceRmlUi::ElemButton::setChecked(button, enabled);
						persistAudioMidiSettings();
					}
				});
				parent->InsertBefore(std::move(entry), inputTemplate);
			}
		}
	}

	void Editor::populateSkinSettings()
	{
		auto* firstEntry = m_settingsRoot ? m_settingsRoot->GetElementById("hardwareSkinEntry") : nullptr;
		if(!firstEntry)
			return;
		auto* parent = firstEntry->GetParentNode();
		// Keep the header and hidden template, replacing only generated skin rows.
		while(parent->GetNumChildren() > 2)
			parent->RemoveChild(parent->GetChild(1));
		for(const auto& skin : findSkins())
		{
			auto entry = firstEntry->Clone();
			auto* element = entry.get();
			element->SetId("");
			element->RemoveProperty(Rml::PropertyId::Display);
			element->SetPseudoClass("current", skin == m_skin);
			juceRmlUi::helper::findChild(element, "lbName")->SetInnerRML(Rml::StringUtilities::EncodeRml(skin.displayName));
			juceRmlUi::helper::findChild(element, "lbType")->SetInnerRML(skin.folder.empty() ? "Embedded" : "Disk");
			auto* activate = juceRmlUi::helper::findChild(element, "btActivate");
			auto* exportButton = juceRmlUi::helper::findChild(element, "btExport");
			juceRmlUi::helper::setVisible(activate, !(skin == m_skin));
			juceRmlUi::helper::setVisible(exportButton, skin.folder.empty());
			juceRmlUi::EventListener::AddClick(activate, [this, skin]
			{
				const juce::WeakReference<Editor> safeThis(this);
				juce::MessageManager::callAsync([safeThis, skin]
				{
					if(auto* editor = safeThis.get())
						editor->loadSkin(skin);
				});
			});
			juceRmlUi::EventListener::AddClick(exportButton, [this]
			{
				exportEmbeddedSkin();
			});
			parent->InsertBefore(std::move(entry), firstEntry);
		}
	}

	void Editor::showSettings(const bool _show)
	{
		if(!_show)
		{
			m_settingsRoot = nullptr;
			if(!m_settingsWindow)
				return;
			m_settingsWindow->setVisible(false);
			const juce::WeakReference<Editor> safeThis(this);
			juce::MessageManager::callAsync([safeThis]
			{
				if(auto* editor = safeThis.get())
					if(editor->m_settingsWindow && !editor->m_settingsWindow->isVisible())
						editor->m_settingsWindow.reset();
			});
			return;
		}
		if(m_settingsWindow)
		{
			m_settingsRoot = m_settingsWindow->settingsRoot();
			m_settingsWindow->setVisible(true);
			m_settingsWindow->toFront(true);
			return;
		}

		m_settingsWindow = std::make_unique<SettingsWindow>(*this);
		initialiseSettings("pageSettingsSkin", "btSettingsSkin");
	}

	void Editor::initialiseSettings(const char* _pageId, const char* _buttonId)
	{
		if(!m_settingsWindow)
			return;
		juceRmlUi::RmlInterfaces::ScopedAccess access(m_settingsWindow->rmlComponent());
		m_settingsRoot = m_settingsWindow->settingsRoot();
		if(!m_settingsRoot)
		{
			m_settingsWindow.reset();
			return;
		}
		juceRmlUi::EventListener::Add(m_settingsRoot, Rml::EventId::Keydown, [this](Rml::Event& _event)
		{
			if(juceRmlUi::helper::getKeyIdentifier(_event) == Rml::Input::KI_ESCAPE)
			{
				showSettings(false);
				_event.StopPropagation();
			}
		});
		for(const auto& page : {std::pair{"btSettingsSkin", "pageSettingsSkin"},
		                         std::pair{"btSettingsGui", "pageSettingsGui"},
		                         std::pair{"btSettingsAudio", "pageSettingsAudio"},
		                         std::pair{"btSettingsMidi", "pageSettingsMidi"},
		                         std::pair{"btSettingsDeveloper", "pageSettingsDeveloper"}})
			juceRmlUi::EventListener::AddClick(m_settingsRoot->GetElementById(page.first), [this, page]
			{
				selectSettingsPage(page.second, page.first);
			});

		if(auto* openFolder = m_settingsRoot->GetElementById("btOpenSkinFolder"))
			juceRmlUi::EventListener::AddClick(openFolder, [this]
			{
				const auto folder = juce::File(skinFolder());
				(void)folder.createDirectory();
				folder.revealToUser();
			});

		const auto wireToggle = [this](const char* _id, const bool _enabled, auto _onChange)
		{
			auto* container = m_settingsRoot->GetElementById(_id);
			auto* button = container ? juceRmlUi::helper::findChild(container, "button") : nullptr;
			if(!container || !button)
				return;
			juceRmlUi::ElemButton::setChecked(button, _enabled);
			juceRmlUi::EventListener::AddClick(container,
				[button, onChange = std::move(_onChange)]
				{
					const auto enabled = !juceRmlUi::ElemButton::isChecked(button);
					juceRmlUi::ElemButton::setChecked(button, enabled);
					onChange(enabled);
				});
		};

		wireToggle("btReloadViaF5", m_processor.config().getBoolValue("reloadSkinViaF5", false),
			[this](const bool _enabled)
			{
				m_processor.config().setValue("reloadSkinViaF5", _enabled);
				m_processor.config().saveIfNeeded();
			});
		wireToggle("btEnableRmlUiDebugger", m_processor.config().getBoolValue("enableRmlUiDebugger", false),
			[this](const bool _enabled)
			{
				m_processor.config().setValue("enableRmlUiDebugger", _enabled);
				m_processor.config().saveIfNeeded();
				if(m_rml)
				{
					juceRmlUi::RmlInterfaces::ScopedAccess access(*m_rml);
					m_rml->enableDebugger(_enabled);
				}
			});
		wireToggle("btForceSoftwareRendering", m_processor.config().getIntValue("forceSoftwareRenderer", -1) > 0,
			[this](const bool _enabled)
			{
				m_processor.config().setValue("forceSoftwareRenderer", _enabled ? 1 : 0);
				m_processor.config().saveIfNeeded();
				// The renderer is chosen when a window's RmlUi component is created, so both
				// windows are rebuilt: the panel here, and this settings window right after -
				// it keeps drawing with the old renderer otherwise, until it is reopened.
				const juce::WeakReference<Editor> safeThis(this);
				juce::MessageManager::callAsync([safeThis]
				{
					if(auto* editor = safeThis.get())
					{
						const auto skin = editor->m_skin;
						editor->loadSkin(skin);
						editor->reopenSettings("pageSettingsGui", "btSettingsGui");
					}
				});
			});

		const auto currentScale = m_processor.config().getIntValue("scale", 100);
		for(const auto scale : g_guiScales)
			if(auto* container = m_settingsRoot->GetElementById("btScale" + std::to_string(scale)))
			{
				auto* button = juceRmlUi::helper::findChild(container, "button");
				juceRmlUi::ElemButton::setChecked(button, scale == currentScale);
				juceRmlUi::EventListener::AddClick(container, [this, scale]
				{
					setGuiScale(scale);
					for(const auto value : g_guiScales)
						if(auto* choice = m_settingsRoot->GetElementById("btScale" + std::to_string(value)))
							juceRmlUi::ElemButton::setChecked(
								juceRmlUi::helper::findChild(choice, "button"), value == scale);
				});
			}

		const std::array resamplers{
			std::pair{"btResamplerLegacy", synthLib::Resampler::Mode::Legacy},
			std::pair{"btResamplerHq", synthLib::Resampler::Mode::MameHq},
			std::pair{"btResamplerLofi", synthLib::Resampler::Mode::MameLofi}};
		for(const auto& entry : resamplers)
		{
			const auto* id = entry.first;
			const auto mode = entry.second;
			if(auto* container = m_settingsRoot->GetElementById(id))
			{
				auto* button = juceRmlUi::helper::findChild(container, "button");
				juceRmlUi::ElemButton::setChecked(button, mode == m_processor.resamplerMode());
				juceRmlUi::EventListener::AddClick(container, [this, mode, resamplers]
				{
					m_processor.setResamplerMode(mode);
					for(const auto& choice : resamplers)
						if(auto* element = m_settingsRoot->GetElementById(choice.first))
							juceRmlUi::ElemButton::setChecked(
								juceRmlUi::helper::findChild(element, "button"), choice.second == mode);
				});
			}
		}
		populateAudioMidiSettings();
		populateSkinSettings();
		selectSettingsPage(_pageId, _buttonId);
		m_settingsWindow->setVisible(true);
		m_settingsWindow->toFront(true);
	}
}

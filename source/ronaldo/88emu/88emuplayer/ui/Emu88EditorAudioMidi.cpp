#include "88emuplayer/ui/Emu88Editor.h"
#include "88emuplayer/ui/Emu88EditorBindings.h"
#include "88emuplayer/ui/Emu88EditorWindows.h"
#include "RmlUi/Core/StringUtilities.h"
#include "jucePlayerLib/audioRouting.h"
#include "jucePlayerLib/midiInputRouting.h"
#include "jucePlayerLib/portMidiBridge.h"
#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceUiLib/messageBox.h"
#include "juce_audio_utils/juce_audio_utils.h"

#include "juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h"

#include "RmlUi/Core/Elements/ElementFormControlInput.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>

namespace emu88Player
{
    void Editor::populateAudioSettings()
    {
        auto* holder = juce::StandalonePluginHolder::getInstance();
        if (!holder || !m_settingsRoot)
            return;
        auto& manager = holder->deviceManager;
        if (m_processor.audioRouting())
        {
            juce::Component::SafePointer<Editor> safe(this);
            m_processor.audioRouting()->onDeviceChanged = [safe]
            {
                if (safe && safe->m_settingsRoot)
                    if (auto* page = safe->m_settingsRoot->GetElementById("pageSettingsAudio");
                        page && page->IsVisible())
                        safe->reopenSettings("pageSettingsAudio", "btSettingsAudio");
            };
        }


        const auto combo = [this](const char* _id)
        { return dynamic_cast<juceRmlUi::ElemComboBox*>(m_settingsRoot->GetElementById(_id)); };
        const auto initialiseCombo =
            [](juceRmlUi::ElemComboBox* _combo, const std::vector<Rml::String>& _labels, const int _selected)
        {
            if (!_combo)
                return;
            _combo->setOptions(_labels);
            if (!_labels.empty())
                _combo->setSelectedIndex(
                    static_cast<size_t>(std::clamp(_selected, 0, static_cast<int>(_labels.size()) - 1)), false);
        };

        std::vector<juce::String> typeNames;
        std::vector<Rml::String> typeLabels;
        int selectedType = 0;
        const auto& types = manager.getAvailableDeviceTypes();
        for (int i = 0; i < types.size(); ++i)
        {
            typeNames.push_back(types[i]->getTypeName());
            typeLabels.push_back(typeNames.back().toStdString());
            if (typeNames.back() == manager.getCurrentAudioDeviceType())
                selectedType = i;
        }
        if (auto* typeCombo = combo("audioDeviceType"))
        {
            initialiseCombo(typeCombo, typeLabels, selectedType);
            juceRmlUi::EventListener::Add(
                typeCombo, Rml::EventId::Change,
                [this, typeCombo, typeNames](Rml::Event&)
                {
                    const auto index = typeCombo->getSelectedIndex();
                    if (index < 0 || index >= static_cast<int>(typeNames.size()))
                        return;
                    if (auto* currentHolder = juce::StandalonePluginHolder::getInstance())
                    {
                        if (m_processor.audioRouting())
                            m_processor.audioRouting()->disableSystem();
                        const auto error = jucePlayer::selectAudioDeviceType(currentHolder->deviceManager,
                                                                             typeNames[static_cast<size_t>(index)],
                                                                             m_processor.getMainBusNumOutputChannels());
                        if (error.isNotEmpty())
                            genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "88emuPlayer",
                                                          error.toStdString());
                        if (m_processor.audioRouting())
                            m_processor.audioRouting()->restoreChannels();
                        persistAudioMidiSettings();
                        reopenSettings("pageSettingsAudio", "btSettingsAudio");
                    }
                });
        }

        juce::AudioIODeviceType* currentType = nullptr;
        for (auto* type : types)
            if (type->getTypeName() == manager.getCurrentAudioDeviceType())
            {
                currentType = type;
                break;
            }

        const auto setup = manager.getAudioDeviceSetup();
        juce::StringArray outputNames;
        if (currentType)
        {
            currentType->scanForDevices();
            outputNames = currentType->getDeviceNames(false);
        }
        const bool canFollowSystem = jucePlayer::AudioRouting::supportsSystem(manager.getCurrentAudioDeviceType());
        std::vector<Rml::String> outputLabels{"<none>"};
        if (canFollowSystem)
            outputLabels.push_back(
                "Use system device" +
                std::string(m_processor.audioRouting() && m_processor.audioRouting()->followsSystem() ? ": " +
                                    (setup.outputDeviceName.isEmpty() ? std::string("unavailable")
                                                                      : setup.outputDeviceName.toStdString())
                                                                                                      : ""));
        int selectedOutput = 0;
        for (int i = 0; i < outputNames.size(); ++i)
        {
            outputLabels.push_back(outputNames[i].toStdString());
            if (outputNames[i] == setup.outputDeviceName)
                selectedOutput = i + (canFollowSystem ? 2 : 1);
        }
        if (canFollowSystem && m_processor.audioRouting() && m_processor.audioRouting()->followsSystem())
            selectedOutput = 1;
        if (auto* outputCombo = combo("audioOutputDevice"))
        {
            initialiseCombo(outputCombo, outputLabels, selectedOutput);
            juceRmlUi::EventListener::Add(
                outputCombo, Rml::EventId::Change,
                [this, outputCombo, outputNames, canFollowSystem](Rml::Event&)
                {
                    const auto selected = outputCombo->getSelectedIndex();
                    const bool system = canFollowSystem && selected == 1;
                    const auto index = selected - (canFollowSystem ? 2 : 1);
                    if (selected < 0 || index >= outputNames.size())
                        return;
                    if (auto* currentHolder = juce::StandalonePluginHolder::getInstance())
                    {
                        const auto name = index >= 0 ? outputNames[index] : juce::String{};
                        const auto error = m_processor.audioRouting()
                            ? m_processor.audioRouting()->selectOutput(name, system)
                            : jucePlayer::selectAudioOutputDevice(currentHolder->deviceManager, name);
                        if (error.isNotEmpty())
                            genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "88emuPlayer",
                                                          error.toStdString());
                        persistAudioMidiSettings();
                        reopenSettings("pageSettingsAudio", "btSettingsAudio");
                    }
                });
        }

        if (auto* warning = m_settingsRoot->GetElementById("audioChannelWarning"))
        {
            const auto text =
                m_processor.audioRouting() ? m_processor.audioRouting()->channelWarning() : juce::String{};
            warning->SetInnerRML(Rml::StringUtilities::EncodeRml(text.toStdString()));
            warning->SetProperty("display", text.isEmpty() ? "none" : "block");
        }

        if (auto* device = manager.getCurrentAudioDevice())
        {
            const auto channelNames = device->getOutputChannelNames();
            std::vector<Rml::String> channelLabels;
            for (int i = 0; i < channelNames.size(); ++i)
                channelLabels.push_back(std::to_string(i + 1) + ": " + channelNames[i].toStdString());
            const auto selectedChannels =
                m_processor.audioRouting() ? m_processor.audioRouting()->channels() : std::make_pair(0, 1);
            for (const bool left : {true, false})
                if (auto* channelCombo = combo(left ? "audioLeftChannel" : "audioRightChannel"))
                {
                    initialiseCombo(channelCombo, channelLabels,
                                    left ? selectedChannels.first : selectedChannels.second);
                    juceRmlUi::EventListener::Add(channelCombo, Rml::EventId::Change,
                                                  [this, channelCombo, left](Rml::Event&)
                                                  {
                                                      if (!m_processor.audioRouting())
                                                          return;
                                                      auto [l, r] = m_processor.audioRouting()->channels();
                                                      const auto selected = channelCombo->getSelectedIndex();
                                                      if (left)
                                                      {
                                                          if (selected == r)
                                                              r = l;
                                                          l = selected;
                                                      }
                                                      else
                                                      {
                                                          if (selected == l)
                                                              l = r;
                                                          r = selected;
                                                      }
                                                      const auto error = m_processor.audioRouting()->setChannels(l, r);
                                                      if (error.isNotEmpty())
                                                          genericUI::MessageBox::showOk(
                                                              genericUI::MessageBox::Icon::Warning, "88emuPlayer",
                                                              error.toStdString());
                                                      reopenSettings("pageSettingsAudio", "btSettingsAudio");
                                                  });
                }
            const auto rates = device->getAvailableSampleRates();
            std::vector<Rml::String> rateLabels;
            int selectedRate = 0;
            for (int i = 0; i < rates.size(); ++i)
            {
                rateLabels.push_back(juce::String(rates[i], rates[i] == std::floor(rates[i]) ? 0 : 1).toStdString() +
                                     " Hz");
                if (std::abs(rates[i] - device->getCurrentSampleRate()) < 0.5)
                    selectedRate = i;
            }
            if (auto* rateCombo = combo("audioSampleRate"))
            {
                initialiseCombo(rateCombo, rateLabels, selectedRate);
                juceRmlUi::EventListener::Add(
                    rateCombo, Rml::EventId::Change,
                    [this, rateCombo, rates](Rml::Event&)
                    {
                        const auto index = rateCombo->getSelectedIndex();
                        if (index < 0 || index >= rates.size())
                            return;
                        if (auto* currentHolder = juce::StandalonePluginHolder::getInstance())
                        {
                            auto newSetup = currentHolder->deviceManager.getAudioDeviceSetup();
                            newSetup.sampleRate = rates[index];
                            const auto error = currentHolder->deviceManager.setAudioDeviceSetup(newSetup, true);
                            if (error.isNotEmpty())
                                genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "88emuPlayer",
                                                              error.toStdString());
                            persistAudioMidiSettings();
                            reopenSettings("pageSettingsAudio", "btSettingsAudio");
                        }
                    });
            }

            const auto sizes = device->getAvailableBufferSizes();
            std::vector<Rml::String> sizeLabels;
            int selectedSize = 0;
            for (int i = 0; i < sizes.size(); ++i)
            {
                sizeLabels.push_back(std::to_string(sizes[i]) + " samples");
                if (sizes[i] == device->getCurrentBufferSizeSamples())
                    selectedSize = i;
            }
            if (auto* sizeCombo = combo("audioBufferSize"))
            {
                initialiseCombo(sizeCombo, sizeLabels, selectedSize);
                juceRmlUi::EventListener::Add(
                    sizeCombo, Rml::EventId::Change,
                    [this, sizeCombo, sizes](Rml::Event&)
                    {
                        const auto index = sizeCombo->getSelectedIndex();
                        if (index < 0 || index >= sizes.size())
                            return;
                        if (auto* currentHolder = juce::StandalonePluginHolder::getInstance())
                        {
                            auto newSetup = currentHolder->deviceManager.getAudioDeviceSetup();
                            newSetup.bufferSize = sizes[index];
                            const auto error = currentHolder->deviceManager.setAudioDeviceSetup(newSetup, true);
                            if (error.isNotEmpty())
                                genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "88emuPlayer",
                                                              error.toStdString());
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
            if (auto* row = m_settingsRoot->GetElementById("audioControlPanelRow"))
                juceRmlUi::helper::setVisible(row, hasPanel);
            if (auto* panelButton = m_settingsRoot->GetElementById("btAudioControlPanel"); panelButton && hasPanel)
            {
                juceRmlUi::EventListener::AddClick(
                    panelButton,
                    [this]
                    {
                        auto* currentHolder = juce::StandalonePluginHolder::getInstance();
                        auto* currentDevice =
                            currentHolder ? currentHolder->deviceManager.getCurrentAudioDevice() : nullptr;
                        if (!currentDevice || !currentDevice->hasControlPanel())
                            return;
                        // Block interaction with the settings while a native driver panel
                        // pumps messages, just as JUCE's stock device selector does.
                        juce::Component modalWindow;
                        modalWindow.addToDesktop(0);
                        modalWindow.enterModalState();
                        const auto changed = jucePlayer::showAudioDeviceControlPanel(currentHolder->deviceManager);
                        if (changed)
                        {
                            if (m_processor.audioRouting())
                                m_processor.audioRouting()->restoreChannels();
                            persistAudioMidiSettings();
                        }
                        reopenSettings("pageSettingsAudio", "btSettingsAudio");
                    });
            }
        }
    }

    void Editor::populateMidiSettings()
    {
        auto* holder = juce::StandalonePluginHolder::getInstance();
        if (!holder || !m_settingsRoot)
            return;
        auto& manager = holder->deviceManager;
        const auto combo = [this](const char* _id)
        { return dynamic_cast<juceRmlUi::ElemComboBox*>(m_settingsRoot->GetElementById(_id)); };
        const auto initialiseCombo =
            [](juceRmlUi::ElemComboBox* _combo, const std::vector<Rml::String>& _labels, const int _selected)
        {
            if (!_combo)
                return;
            _combo->setOptions(_labels);
            if (!_labels.empty())
                _combo->setSelectedIndex(
                    static_cast<size_t>(std::clamp(_selected, 0, static_cast<int>(_labels.size()) - 1)), false);
        };

        auto midiOutputs = juce::MidiOutput::getAvailableDevices();
        for (int i = midiOutputs.size(); --i >= 0;)
            if (jucePlayer::PortMidiBridge::isOwnVirtualPortName(midiOutputs[i].name))
                midiOutputs.remove(i);
        std::vector<Rml::String> midiOutputLabels{"<none>"};
        int selectedMidiOutput = 0;
        for (int i = 0; i < midiOutputs.size(); ++i)
        {
            midiOutputLabels.push_back(midiOutputs[i].name.toStdString());
            if (midiOutputs[i].identifier == manager.getDefaultMidiOutputIdentifier())
                selectedMidiOutput = i + 1;
        }
        if (auto* midiOutputCombo = combo("midiOutputDevice"))
        {
            initialiseCombo(midiOutputCombo, midiOutputLabels, selectedMidiOutput);
            juceRmlUi::EventListener::Add(
                midiOutputCombo, Rml::EventId::Change,
                [this, midiOutputCombo, midiOutputs](Rml::Event&)
                {
                    const auto index = midiOutputCombo->getSelectedIndex() - 1;
                    if (auto* currentHolder = juce::StandalonePluginHolder::getInstance())
                    {
                        currentHolder->deviceManager.setDefaultMidiOutputDevice(
                            index >= 0 && index < midiOutputs.size() ? midiOutputs[index].identifier : juce::String{});
                        persistAudioMidiSettings();
                    }
                });
        }

        // PortMidi can only create virtual endpoints on CoreMIDI and ALSA, so on
        // Windows the switch would be a no-op. Say that instead of offering it.
        const auto virtualPorts = jucePlayer::PortMidiBridge::virtualPortsSupported();
        if (auto* unsupported = m_settingsRoot->GetElementById("portMidiUnsupported"))
            juceRmlUi::helper::setVisible(unsupported, !virtualPorts);
        if (auto* portMidi = m_settingsRoot->GetElementById("btPortMidi"))
        {
            juceRmlUi::helper::setVisible(portMidi, virtualPorts);
            if (virtualPorts)
            {
                auto* button = juceRmlUi::helper::findChild(portMidi, "button");
                juceRmlUi::ElemButton::setChecked(button, m_processor.portMidiEnabled());
                juceRmlUi::EventListener::AddClick(portMidi,
                                                   [this, button]
                                                   {
                                                       const auto enabled = !m_processor.portMidiEnabled();
                                                       m_processor.setPortMidiEnabled(enabled);
                                                       juceRmlUi::ElemButton::setChecked(button, enabled);
                                                   });
            }
        }

        auto* inputTemplate = m_settingsRoot->GetElementById("midiInputEntry");
        if (inputTemplate)
        {
            auto* parent = inputTemplate->GetParentNode();
            auto midiInputs = juce::MidiInput::getAvailableDevices();
            for (int i = midiInputs.size(); --i >= 0;)
                if (jucePlayer::PortMidiBridge::isOwnVirtualPortName(midiInputs[i].name))
                    midiInputs.remove(i);
            if (auto* empty = m_settingsRoot->GetElementById("midiInputEmpty"))
                juceRmlUi::helper::setVisible(empty, midiInputs.isEmpty());
            // Groups the current device lacks stay selectable, dimmed: their messages are dropped.
            const auto deviceGroups = emu88Lib::getDeviceProfile(m_processor.deviceModel()).groupCount;
            for (int i = 0; i < midiInputs.size(); ++i)
            {
                const auto& device = midiInputs[i];
                auto entry = inputTemplate->Clone();
                entry->SetId("midiInput" + std::to_string(i));
                entry->RemoveProperty(Rml::PropertyId::Display);
                auto* label = juceRmlUi::helper::findChild(entry.get(), "label");
                label->SetInnerRML(Rml::StringUtilities::EncodeRml(device.name.toStdString()));
                const auto groups =
                    m_processor.midiInputRouting() ? m_processor.midiInputRouting()->groups(device.identifier) : 0;
                for (uint8_t group = 0; group < 4; ++group)
                {
                    const auto groupName = std::string(1, static_cast<char>('A' + group));
                    auto* button = juceRmlUi::helper::findChild(entry.get(), "group" + groupName);
                    if (!button)
                        continue;
                    juceRmlUi::ElemButton::setChecked(button, ((groups >> group) & 1) != 0);
                    if (group >= deviceGroups)
                    {
                        button->SetClass("unavailable", true);
                        button->SetAttribute("title", "Part group " + groupName + ": not on this device");
                    }
                    juceRmlUi::EventListener::AddClick(
                        button,
                        [this, button, group, device]
                        {
                            if (!m_processor.midiInputRouting())
                                return;
                            m_processor.midiInputRouting()->setGroups(
                                device,
                                static_cast<uint8_t>(m_processor.midiInputRouting()->groups(device.identifier) ^
                                                     (1u << group)));
                            // Read back: the input may have failed to open.
                            const auto now = m_processor.midiInputRouting()->groups(device.identifier);
                            juceRmlUi::ElemButton::setChecked(button, ((now >> group) & 1) != 0);
                            persistAudioMidiSettings();
                        });
                }
                parent->InsertBefore(std::move(entry), inputTemplate);
            }
        }
    }

} // namespace emu88Player

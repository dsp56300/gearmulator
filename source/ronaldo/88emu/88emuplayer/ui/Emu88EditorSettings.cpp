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
        if (!holder)
            return;

        auto* active = dynamic_cast<juce::PropertiesFile*>(holder->settings.get());
        if (active && active->getFile() == m_processor.config().getFile())
        {
            m_processor.useConfig(*active);
            return;
        }

        auto config = m_processor.takeConfigOwnership();
        if (!config)
            return;

        auto* adoptedConfig = config.get();
        holder->settings.setOwned(config.release());
        m_processor.useConfig(*adoptedConfig);

        // The stock standalone wrapper opens its audio devices before creating the
        // editor. Apply the consolidated Documents state once after taking over its
        // settings so a stale legacy JUCE settings file cannot win at startup.
        if (auto savedState = adoptedConfig->getXmlValue("audioSetup"))
        {
            const auto error =
                holder->deviceManager.initialise(0, m_processor.getMainBusNumOutputChannels(), savedState.get(), true);
            if (error.isNotEmpty())
                juce::Logger::writeToLog("Unable to restore 88emuPlayer audio/MIDI settings: " + error);
        }
    }

    void Editor::persistAudioMidiSettings()
    {
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            auto state = holder->deviceManager.createStateXml();
            m_processor.config().setValue("audioSetup", state.get());
            m_processor.config().saveIfNeeded();
        }
    }

    void Editor::reopenSettings(const char* _pageId, const char* _buttonId)
    {
        const juce::WeakReference<Editor> safeThis(this);
        juce::MessageManager::callAsync(
            [safeThis, _pageId, _buttonId]
            {
                auto* editor = safeThis.get();
                if (!editor)
                    return;
                if (!editor->m_settingsWindow)
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
        if (!m_settingsRoot)
            return;
        for (const auto* page : {"pageSettingsGeneral", "pageSettingsSkin", "pageSettingsGui", "pageSettingsAudio",
                                 "pageSettingsMidi", "pageSettingsDeveloper"})
            if (auto* element = m_settingsRoot->GetElementById(page))
            {
                if (std::string(page) == _pageId)
                    element->RemoveProperty(Rml::PropertyId::Display);
                else
                    element->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
            }
        for (const auto* button : {"btSettingsGeneral", "btSettingsSkin", "btSettingsGui", "btSettingsAudio",
                                   "btSettingsMidi", "btSettingsDeveloper"})
            if (auto* element = m_settingsRoot->GetElementById(button))
                element->SetPseudoClass("checked", std::string(button) == _buttonId);
    }

    void Editor::populateSkinSettings()
    {
        auto* firstEntry = m_settingsRoot ? m_settingsRoot->GetElementById("hardwareSkinEntry") : nullptr;
        if (!firstEntry)
            return;
        auto* parent = firstEntry->GetParentNode();
        // Keep the header and hidden template, replacing only generated skin rows.
        while (parent->GetNumChildren() > 2)
            parent->RemoveChild(parent->GetChild(1));
        for (const auto& skin : findSkins())
        {
            auto entry = firstEntry->Clone();
            auto* element = entry.get();
            element->SetId("");
            element->RemoveProperty(Rml::PropertyId::Display);
            element->SetPseudoClass("current", skin == m_skin);
            juceRmlUi::helper::findChild(element, "lbName")
                ->SetInnerRML(Rml::StringUtilities::EncodeRml(skin.displayName));
            juceRmlUi::helper::findChild(element, "lbType")->SetInnerRML(skin.folder.empty() ? "Embedded" : "Disk");
            auto* activate = juceRmlUi::helper::findChild(element, "btActivate");
            auto* exportButton = juceRmlUi::helper::findChild(element, "btExport");
            juceRmlUi::helper::setVisible(activate, !(skin == m_skin));
            juceRmlUi::helper::setVisible(exportButton, skin.folder.empty());
            juceRmlUi::EventListener::AddClick(activate,
                                               [this, skin]
                                               {
                                                   const juce::WeakReference<Editor> safeThis(this);
                                                   juce::MessageManager::callAsync(
                                                       [safeThis, skin]
                                                       {
                                                           if (auto* editor = safeThis.get())
                                                               editor->loadSkin(skin);
                                                       });
                                               });
            juceRmlUi::EventListener::AddClick(exportButton, [this] { exportEmbeddedSkin(); });
            parent->InsertBefore(std::move(entry), firstEntry);
        }
    }

    void Editor::showSettings(const bool _show)
    {
        if (!_show)
        {
            m_settingsRoot = nullptr;
            if (!m_settingsWindow)
                return;
            m_settingsWindow->setVisible(false);
            const juce::WeakReference<Editor> safeThis(this);
            juce::MessageManager::callAsync(
                [safeThis]
                {
                    if (auto* editor = safeThis.get())
                        if (editor->m_settingsWindow && !editor->m_settingsWindow->isVisible())
                            editor->m_settingsWindow.reset();
                });
            return;
        }
        if (m_settingsWindow)
        {
            m_settingsRoot = m_settingsWindow->settingsRoot();
            m_settingsWindow->setVisible(true);
            m_settingsWindow->toFront(true);
            return;
        }

        m_settingsWindow = std::make_unique<SettingsWindow>(*this);
        initialiseSettings("pageSettingsGeneral", "btSettingsGeneral");
    }

    void Editor::initialiseSettings(const char* _pageId, const char* _buttonId)
    {
        if (!m_settingsWindow)
            return;
        juceRmlUi::RmlInterfaces::ScopedAccess access(m_settingsWindow->rmlComponent());
        m_settingsRoot = m_settingsWindow->settingsRoot();
        if (!m_settingsRoot)
        {
            m_settingsWindow.reset();
            return;
        }
        juceRmlUi::EventListener::Add(m_settingsRoot, Rml::EventId::Keydown,
                                      [this](Rml::Event& _event)
                                      {
                                          if (juceRmlUi::helper::getKeyIdentifier(_event) == Rml::Input::KI_ESCAPE)
                                          {
                                              showSettings(false);
                                              _event.StopPropagation();
                                          }
                                      });
        for (const auto& page :
             {std::pair{"btSettingsGeneral", "pageSettingsGeneral"}, std::pair{"btSettingsSkin", "pageSettingsSkin"},
              std::pair{"btSettingsGui", "pageSettingsGui"}, std::pair{"btSettingsAudio", "pageSettingsAudio"},
              std::pair{"btSettingsMidi", "pageSettingsMidi"},
              std::pair{"btSettingsDeveloper", "pageSettingsDeveloper"}})
            juceRmlUi::EventListener::AddClick(m_settingsRoot->GetElementById(page.first),
                                               [this, page] { selectSettingsPage(page.second, page.first); });

        if (auto* openFolder = m_settingsRoot->GetElementById("btOpenSkinFolder"))
            juceRmlUi::EventListener::AddClick(openFolder,
                                               [this]
                                               {
                                                   const auto folder = juce::File(skinFolder());
                                                   (void)folder.createDirectory();
                                                   folder.revealToUser();
                                               });

        const auto wireToggle = [this](const char* _id, const bool _enabled, auto _onChange)
        {
            auto* container = m_settingsRoot->GetElementById(_id);
            auto* button = container ? juceRmlUi::helper::findChild(container, "button") : nullptr;
            if (!container || !button)
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

        if (auto* reset = dynamic_cast<juceRmlUi::ElemComboBox*>(m_settingsRoot->GetElementById("songResetMode")))
        {
            reset->setOptions({"Off", "GM", "GS", "MT-32 tones (GS)"});
            reset->setSelectedIndex(static_cast<size_t>(m_processor.midiPlayer().resetMode()), false);
            juceRmlUi::EventListener::Add(
                reset, Rml::EventId::Change,
                [this, reset](Rml::Event&)
                {
                    const auto index = reset->getSelectedIndex();
                    if (index < 0 || index > static_cast<int>(jucePlayer::MidiPlayer::ResetMode::Mt32))
                        return;
                    m_processor.midiPlayer().setResetMode(static_cast<jucePlayer::MidiPlayer::ResetMode>(index));
                    m_processor.config().setValue("songResetMode", index);
                    m_processor.config().saveIfNeeded();
                });
        }
        if (auto* gap = dynamic_cast<Rml::ElementFormControlInput*>(m_settingsRoot->GetElementById("songGapMs")))
        {
            gap->SetValue(std::to_string(m_processor.midiPlayer().songGapMs()));
            juceRmlUi::EventListener::Add(
                gap, Rml::EventId::Change,
                [this, gap](Rml::Event&)
                {
                    const auto value = gap->GetValue();
                    uint32_t milliseconds = 0;
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), milliseconds);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                        return;
                    m_processor.midiPlayer().setSongGapMs(milliseconds);
                    m_processor.config().setValue("songGapMs", static_cast<int>(m_processor.midiPlayer().songGapMs()));
                    m_processor.config().saveIfNeeded();
                });
            juceRmlUi::EventListener::Add(gap, Rml::EventId::Blur, [this, gap](Rml::Event&)
                                          { gap->SetValue(std::to_string(m_processor.midiPlayer().songGapMs())); });
        }

        wireToggle("btOutputLimiter", m_processor.outputLimiterEnabled(),
                   [this](bool enabled) { m_processor.setOutputLimiterEnabled(enabled); });

        wireToggle("btWarnRomHashMismatch", m_processor.config().getBoolValue("warnRomHashMismatch", true),
                   [this](const bool _enabled)
                   {
                       m_processor.config().setValue("warnRomHashMismatch", _enabled);
                       m_processor.config().saveIfNeeded();
                   });

        // Both are taken up by the next device load, Power on or Restart.
        wireToggle("btFactoryResetOnLoad", m_processor.bootOptions().factoryReset,
                   [this](const bool _enabled)
                   {
                       auto boot = m_processor.bootOptions();
                       boot.factoryReset = _enabled;
                       m_processor.setBootOptions(boot);
                   });
        wireToggle("btFastBoot", m_processor.bootOptions().fastBoot,
                   [this](const bool _enabled)
                   {
                       auto boot = m_processor.bootOptions();
                       boot.fastBoot = _enabled;
                       m_processor.setBootOptions(boot);
                   });

        // A typed path applies on Enter or on leaving the field, and emptying it empties the slot.
        // Leaving it can be the settings window closing, so the work waits until the event is done.
        if (auto* card = dynamic_cast<Rml::ElementFormControlInput*>(m_settingsRoot->GetElementById("pcmCardPath")))
        {
            card->SetValue(m_processor.pcmCardPath());
            const auto apply = [this](const Rml::String& _path)
            {
                const juce::WeakReference<Editor> safeThis(this);
                juce::MessageManager::callAsync(
                    [safeThis, _path]
                    {
                        if (auto* editor = safeThis.get())
                            editor->applyPcmCardPath(_path);
                    });
            };
            juceRmlUi::EventListener::Add(card, Rml::EventId::Change,
                                          [card, apply](Rml::Event& _event)
                                          {
                                              if (_event.GetParameter("linebreak", false))
                                                  apply(card->GetValue());
                                          });
            juceRmlUi::EventListener::Add(card, Rml::EventId::Blur,
                                          [card, apply](Rml::Event&) { apply(card->GetValue()); });
        }
        if (auto* browse = m_settingsRoot->GetElementById("btPcmCardBrowse"))
            juceRmlUi::EventListener::AddClick(browse, [this] { browsePcmCard(); });

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
                       if (m_rml)
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
                       juce::MessageManager::callAsync(
                           [safeThis]
                           {
                               if (auto* editor = safeThis.get())
                               {
                                   const auto skin = editor->m_skin;
                                   editor->loadSkin(skin);
                                   editor->reopenSettings("pageSettingsGui", "btSettingsGui");
                               }
                           });
                   });

        const auto currentScale = m_processor.config().getIntValue("scale", 100);
        for (const auto scale : g_guiScales)
            if (auto* container = m_settingsRoot->GetElementById("btScale" + std::to_string(scale)))
            {
                auto* button = juceRmlUi::helper::findChild(container, "button");
                juceRmlUi::ElemButton::setChecked(button, scale == currentScale);
                juceRmlUi::EventListener::AddClick(
                    container,
                    [this, scale]
                    {
                        setGuiScale(scale);
                        for (const auto value : g_guiScales)
                            if (auto* choice = m_settingsRoot->GetElementById("btScale" + std::to_string(value)))
                                juceRmlUi::ElemButton::setChecked(juceRmlUi::helper::findChild(choice, "button"),
                                                                  value == scale);
                    });
            }

        const std::array resamplers{std::pair{"btResamplerLegacy", synthLib::Resampler::Mode::Legacy},
                                    std::pair{"btResamplerHq", synthLib::Resampler::Mode::MameHq},
                                    std::pair{"btResamplerLofi", synthLib::Resampler::Mode::MameLofi}};
        for (const auto& entry : resamplers)
        {
            const auto* id = entry.first;
            const auto mode = entry.second;
            if (auto* container = m_settingsRoot->GetElementById(id))
            {
                auto* button = juceRmlUi::helper::findChild(container, "button");
                juceRmlUi::ElemButton::setChecked(button, mode == m_processor.resamplerMode());
                juceRmlUi::EventListener::AddClick(
                    container,
                    [this, mode, resamplers]
                    {
                        m_processor.setResamplerMode(mode);
                        for (const auto& choice : resamplers)
                            if (auto* element = m_settingsRoot->GetElementById(choice.first))
                                juceRmlUi::ElemButton::setChecked(juceRmlUi::helper::findChild(element, "button"),
                                                                  choice.second == mode);
                    });
            }
        }
        if (auto* analog = dynamic_cast<juceRmlUi::ElemComboBox*>(m_settingsRoot->GetElementById("analogOutputMode")))
        {
            const auto& modes = emu88Lib::getAnalogOutputModes();
            std::vector<Rml::String> labels;
            size_t selected = 0;
            for (size_t i = 0; i < modes.size(); ++i)
            {
                std::string label = emu88Lib::getAnalogOutputModeName(modes[i]);
                // Auto names the circuit it picks for the current board.
                if (modes[i] == emu88Lib::AnalogOutputMode::Auto)
                    label += std::string(" (") +
                        emu88Lib::getAnalogModelName(emu88Lib::getAutoAnalogModel(m_processor.deviceModel())) + ")";
                labels.push_back(label);
                if (modes[i] == m_processor.analogOutputMode())
                    selected = i;
            }
            analog->setOptions(labels);
            analog->setSelectedIndex(selected, false);
            juceRmlUi::EventListener::Add(analog, Rml::EventId::Change,
                                          [this, analog](Rml::Event&)
                                          {
                                              const auto& choices = emu88Lib::getAnalogOutputModes();
                                              const auto index = analog->getSelectedIndex();
                                              if (index < 0 || index >= static_cast<int>(choices.size()))
                                                  return;
                                              m_processor.setAnalogOutputMode(choices[static_cast<size_t>(index)]);
                                          });
        }
        populateAudioSettings();
        populateMidiSettings();
        populateSkinSettings();
        selectSettingsPage(_pageId, _buttonId);
        m_settingsWindow->setVisible(true);
        m_settingsWindow->toFront(true);
    }

    void Editor::browsePcmCard()
    {
        // Start beside the card in the slot; failing that, among the ROMs, where card dumps usually live.
        const auto current = juce::String::fromUTF8(m_processor.pcmCardPath().c_str());
        auto folder = juce::File::isAbsolutePath(current) ? juce::File(current).getParentDirectory()
                                                          : juce::File(m_processor.romFolder());
        if (!folder.isDirectory())
            folder = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        // Card dumps carry no agreed extension, so every file is offered.
        m_pcmCardChooser = std::make_unique<juce::FileChooser>("Select CM-32P PCM card image", folder, "*", true);
        const juce::WeakReference<Editor> safeThis(this);
        m_pcmCardChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                          juce::FileBrowserComponent::canSelectFiles,
                                      [safeThis](const juce::FileChooser& _chooser)
                                      {
                                          auto* editor = safeThis.get();
                                          if (!editor)
                                              return;
                                          const auto file = _chooser.getResult();
                                          if (file != juce::File())
                                              editor->applyPcmCardPath(file.getFullPathName().toStdString());
                                          editor->m_pcmCardChooser.reset();
                                      });
    }

    void Editor::applyPcmCardPath(const std::string& _path)
    {
        // A pasted path often brings a trailing space or line break along.
        const auto path = juce::String::fromUTF8(_path.c_str()).trim().toStdString();
        std::string error;
        const bool changed = path != m_processor.pcmCardPath();
        const bool applied = !changed || m_processor.setPcmCardPath(path, error);

        // Show the card actually in the slot: the new one, or the old one after a failure.
        if (m_settingsWindow && m_settingsRoot)
        {
            juceRmlUi::RmlInterfaces::ScopedAccess access(m_settingsWindow->rmlComponent());
            if (auto* card = dynamic_cast<Rml::ElementFormControlInput*>(m_settingsRoot->GetElementById("pcmCardPath")))
                card->SetValue(m_processor.pcmCardPath());
        }

        if (!applied)
        {
            genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "CM-32P PCM card", error, this);
            return;
        }
        // A board reads its card only while booting, so a running one restarts to take the new card.
        if (changed && emu88Lib::hasPcmCardSlot(m_processor.deviceModel()) && m_processor.isPoweredOn())
            selectDeviceModel(m_processor.deviceModel(), true);
    }
} // namespace emu88Player

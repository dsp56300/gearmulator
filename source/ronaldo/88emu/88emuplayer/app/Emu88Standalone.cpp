#include <cstdio>
#include <iostream>
#include "88emuplayer/Emu88Processor.h"
#include "88emuplayer/app/Emu88LaunchOptions.h"
#include "88emuplayer/app/Emu88Playlist.h"
#include "88lib/rom/romloader.h"
#include "jucePlayerLib/audioRouting.h"
#include "jucePlayerLib/midiInputRouting.h"
#include "jucePlayerLib/portMidiBridge.h"
#include "juce_audio_utils/juce_audio_utils.h"

#include "juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h"
#if JUCE_WINDOWS
#include <windows.h>
#endif
#include <stdexcept>

namespace emu88Player
{
    namespace
    {
        juce::MidiDeviceInfo resolveMidi(const juce::Array<juce::MidiDeviceInfo>& devices, const std::string& value)
        {
            for (const auto& device : devices)
                if (device.identifier.toStdString() == value)
                    return device;
            std::optional<juce::MidiDeviceInfo> match;
            for (const auto& device : devices)
                if (device.name.toStdString() == value)
                {
                    if (match)
                        throw std::runtime_error("Ambiguous MIDI name '" + value +
                                                 "'; select its ID from --list-endpoints.");
                    match = device;
                }
            if (match)
                return *match;
            throw std::runtime_error("MIDI endpoint not found: " + value + "; use --list-endpoints.");
        }

        void listEndpoints()
        {
            juce::AudioDeviceManager manager;
            for (auto* type : manager.getAvailableDeviceTypes())
            {
                type->scanForDevices();
                std::cout << "Audio backend: " << type->getTypeName() << '\n';
                for (const auto& name : type->getDeviceNames(false))
                    std::cout << "  " << name << '\n';
            }
            for (const auto& info : juce::MidiInput::getAvailableDevices())
                std::cout << "MIDI IN  " << info.identifier << "\t" << info.name << '\n';
            for (const auto& info : juce::MidiOutput::getAvailableDevices())
                std::cout << "MIDI OUT " << info.identifier << "\t" << info.name << '\n';
        }

        std::unique_ptr<juce::XmlElement> prepareDeviceSetup(const LaunchOptions& options, juce::PropertiesFile& config)
        {
            auto xml = config.getXmlValue("audioSetup");
            if (!xml)
                xml = std::make_unique<juce::XmlElement>("DEVICESETUP");
            // Older startup code could persist an empty setup. Treat it as an
            // unconfigured output unless the user explicitly selected None.
            const bool recoverOutput = !options.has("audio-device") &&
                !config.getBoolValue("audioOutputDisabled", false) &&
                xml->getStringAttribute("audioOutputDeviceName", xml->getStringAttribute("audioDeviceName"))
                    .isEmpty() &&
                xml->getStringAttribute("deviceType") != "ASIO";
            if (options.has("audio-device"))
                config.setValue("audioOutputDisabled", options.get("audio-device") == "none");
            if (!config.containsKey("audioSetup") || recoverOutput || options.has("audio-backend") ||
                options.has("audio-device") || jucePlayer::AudioRouting::followsSystem(config))
            {
                const auto previousType = xml->getStringAttribute("deviceType");
                const auto previousName =
                    xml->getStringAttribute("audioOutputDeviceName", xml->getStringAttribute("audioDeviceName"));
                juce::AudioDeviceManager discovery;
                const auto requestedType =
                    juce::String(options.get("audio-backend", xml->getStringAttribute("deviceType").toStdString()));
                juce::AudioIODeviceType* selected = nullptr;
                for (auto* type : discovery.getAvailableDeviceTypes())
                {
                    type->scanForDevices();
                    if (type->getTypeName() == requestedType ||
                        (requestedType.isEmpty() && !selected && !type->getDeviceNames(false).isEmpty()))
                        selected = type;
                }
                if (!selected)
                {
                    if (!requestedType.isEmpty())
                        throw std::runtime_error("Unknown audio backend; use --list-endpoints.");
                    selected = discovery.getAvailableDeviceTypes().getFirst();
                }
                if (!selected)
                    throw std::runtime_error("No audio backends available.");
                xml->setAttribute("deviceType", selected->getTypeName());
                auto name = juce::String(
                    options.get("audio-device", xml->getStringAttribute("audioOutputDeviceName").toStdString()));
                if (!options.has("audio-device") &&
                    (options.has("audio-backend") || !config.containsKey("audioSetup") || recoverOutput))
                {
                    name.clear();
                    if (selected->getTypeName() != "ASIO")
                        name = selected->getDeviceNames(false)[selected->getDefaultDeviceIndex(false)];
                }
                const bool follow = options.has("audio-device") ? name == "system"
                                                                : jucePlayer::AudioRouting::followsSystem(config) &&
                        jucePlayer::AudioRouting::supportsSystem(selected->getTypeName());
                if (follow)
                {
                    if (!jucePlayer::AudioRouting::supportsSystem(selected->getTypeName()))
                        throw std::runtime_error("System output following is unavailable for this backend.");
                    name = jucePlayer::AudioRouting::systemOutput(*selected);
                }
                config.setValue("audioFollowSystem", follow);
                if (name == "none")
                    name.clear();
                if (name.isNotEmpty() && !selected->getDeviceNames(false).contains(name))
                    throw std::runtime_error("Audio output not found: " + name.toStdString() +
                                             "; use --list-endpoints.");
                if (previousType != selected->getTypeName() || previousName != name)
                    for (const auto* attr : {"audioDeviceRate", "audioDeviceBufferSize", "audioDeviceOutChans"})
                        xml->removeAttribute(attr);
                xml->removeAttribute("audioDeviceName");
                xml->setAttribute("audioOutputDeviceName", name);
                xml->setAttribute("audioInputDeviceName", "");
            }
            if (options.has("sample-rate"))
                xml->setAttribute("audioDeviceRate", options.number("sample-rate", 44100));
            if (options.has("buffer-size"))
                xml->setAttribute("audioDeviceBufferSize", static_cast<int>(options.number("buffer-size", 512)));
            if (options.has("output-channels"))
            {
                const auto [left, right] = parseOutputChannels(options.get("output-channels"));
                juce::BigInteger mask;
                mask.setBit(left);
                mask.setBit(right);
                xml->setAttribute("audioDeviceOutChans", mask.toString(2));
            }
            if (!options.midiInputs.empty())
            {
                xml->deleteAllChildElementsWithTagName("MIDIINPUT");
                juce::XmlElement routes("MIDIINPUTGROUPS");
                struct InputRoute
                {
                    juce::MidiDeviceInfo info;
                    uint8_t groups = 0;
                };
                std::vector<InputRoute> selected;
                for (const auto& input : options.midiInputs)
                {
                    if (input.value == "none")
                        continue;
                    const auto info = resolveMidi(juce::MidiInput::getAvailableDevices(), input.value);
                    const auto existing = std::find_if(selected.begin(), selected.end(), [&](const InputRoute& route)
                                                       { return route.info.identifier == info.identifier; });
                    if (existing == selected.end())
                        selected.push_back({info, input.groups});
                    else
                        existing->groups |= input.groups;
                }
                for (const auto& route : selected)
                {
                    auto* device = xml->createNewChildElement("MIDIINPUT");
                    device->setAttribute("name", route.info.name);
                    device->setAttribute("identifier", route.info.identifier);
                    auto* input = routes.createNewChildElement("INPUT");
                    input->setAttribute("name", route.info.name);
                    input->setAttribute("identifier", route.info.identifier);
                    input->setAttribute("groups", route.groups);
                }
                // MidiInputRouting consumes this saved routing table after JUCE opens the selected inputs.
                config.setValue("midiInputGroups", &routes);
            }
            if (options.has("midi-out"))
            {
                const auto info = options.get("midi-out") == "none"
                    ? juce::MidiDeviceInfo{}
                    : resolveMidi(juce::MidiOutput::getAvailableDevices(), options.get("midi-out"));
                xml->setAttribute("defaultMidiOutput", info.name);
                xml->setAttribute("defaultMidiOutputDevice", info.identifier);
            }
            return xml;
        }
    } // namespace

    class StandaloneApplication final : public juce::JUCEApplication, private juce::Timer
    {
    public:
        const juce::String getApplicationName() override { return "88emuPlayer"; }
        const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
        bool moreThanOneInstanceAllowed() override { return true; }
        void anotherInstanceStarted(const juce::String&) override {}
        void initialise(const juce::String&) override
        {
#if JUCE_WINDOWS
            // A GUI-subsystem executable has no console by default. Preserve pipe/file
            // redirection, but make --help and startup errors visible in a parent terminal.
            if (!getCommandLineParameterArray().isEmpty())
            {
                const auto output = GetStdHandle(STD_OUTPUT_HANDLE), error = GetStdHandle(STD_ERROR_HANDLE);
                if (AttachConsole(ATTACH_PARENT_PROCESS))
                {
                    FILE* stream = nullptr;
                    if (!output || output == INVALID_HANDLE_VALUE)
                        freopen_s(&stream, "CONOUT$", "w", stdout);
                    else
                        SetStdHandle(STD_OUTPUT_HANDLE, output);
                    if (!error || error == INVALID_HANDLE_VALUE)
                        freopen_s(&stream, "CONOUT$", "w", stderr);
                    else
                        SetStdHandle(STD_ERROR_HANDLE, error);
                }
            }
#endif
            try
            {
                options = LaunchOptions::parse(getCommandLineParameterArray(), false);
                if (options.has("help"))
                {
                    std::cout << LaunchOptions::help(false);
                    quit();
                    return;
                }
                if (options.has("list-endpoints"))
                {
                    listEndpoints();
                    quit();
                    return;
                }
                if (options.has("rom-dir") && !launchFile(options.get("rom-dir")).isDirectory())
                    throw std::runtime_error("ROM folder does not exist.");
                if (options.has("list-devices"))
                {
                    configureRomSearchPaths(options);
                    listDevices();
                    quit();
                    return;
                }
                if (options.get("virtual-ports") == "on" && !jucePlayer::PortMidiBridge::virtualPortsSupported())
                    throw std::runtime_error("Virtual MIDI ports are not supported on this platform.");
                if (options.number("gain", 1) > Processor::kMaximumOutputGain)
                    throw std::runtime_error("GUI --gain range is 0..2.");
                // Checked here so a bad card is a startup error; the processor loads it for each CM-32P.
                (void)loadPcmCard(options);
                auto requestedAudio = prepareSession();
                auto* processor = openDevices(*requestedAudio);
                jucePlayer::MidiPlayer::AddResult loaded;
                if (options.files.empty())
                {
                    std::vector<std::string> paths;
                    std::string error;
                    if (playlist::read(playlist::defaultFile(), paths, error))
                        loaded = processor->midiPlayer().replaceFiles(paths);
                }
                else
                    loaded = processor->midiPlayer().addFiles(options.files);
                if (!loaded.errors.empty())
                    throw std::runtime_error(loaded.errors.front());
                if (!options.files.empty())
                {
                    std::string error;
                    if (!playlist::write(playlist::defaultFile(), processor->midiPlayer().entries(), error))
                        std::cerr << error << '\n';
                }
                startupSucceeded = true;
                window->setVisible(true);
                if (options.has("play") && loaded.added)
                    startTimer(5000);
            }
            catch (const std::exception& error)
            {
                std::cerr << error.what() << '\n';
                setApplicationReturnValue(2);
                quit();
            }
        }
        void shutdown() override
        {
            stopTimer();
            if (window)
                if (auto* processor = dynamic_cast<Processor*>(window->getPluginHolder()->processor.get()))
                    processor->setRouting(nullptr, nullptr);
            audioRouting.reset();
            midiInputRouting.reset();
            window.reset();
            standaloneConfig = nullptr;
            standaloneLaunch = nullptr;
            if (startupSucceeded && saveTarget != juce::File() && config)
            {
                juce::TemporaryFile replacement(saveTarget);
                if (!saveTarget.getParentDirectory().createDirectory().wasOk() || !config->saveIfNeeded() ||
                    !config->getFile().copyFileTo(replacement.getFile()) ||
                    !replacement.overwriteTargetFileWithTemporary())
                {
                    std::cerr << "Unable to save launch settings to " << saveTarget.getFullPathName() << '\n';
                    setApplicationReturnValue(3);
                }
            }
            config.reset();
            temporary.reset();
        }
        void systemRequestedQuit() override { quit(); }

    private:
        std::unique_ptr<juce::XmlElement> prepareSession()
        {
            juce::PropertiesFile::Options storage;
            storage.millisecondsBeforeSaving = -1;
            const auto sourceFile = launchFile(options.get("config", defaultDataFolder() + "config/88emuPlayer.xml"));

            if (!options.has("config") && !options.sessionOverrides())
                config = Processor::createConfig(defaultDataFolder());
            else
                config = std::make_unique<juce::PropertiesFile>(sourceFile, storage);
            if (sourceFile.existsAsFile() && !config->isValidFile())
                throw std::runtime_error("Invalid config file.");
            if (options.sessionOverrides())
            {
                if (options.has("save-settings"))
                    saveTarget = sourceFile;
                temporary = std::make_unique<juce::TemporaryFile>(".xml");
                auto session = std::make_unique<juce::PropertiesFile>(temporary->getFile(), storage);
                session->addAllPropertiesFrom(*config);
                config = std::move(session);
            }
            if (options.has("device"))
                config->setValue("deviceModel", static_cast<int>(parseDevice(options.get("device"))));
            if (options.has("reset"))
                config->setValue("songResetMode", static_cast<int>(parseReset(options.get("reset"))));
            if (options.has("song-gap-ms"))
                config->setValue("songGapMs", static_cast<int>(options.number("song-gap-ms", 0)));
            if (options.has("limiter"))
                config->setValue("outputLimiter", options.get("limiter") == "on");
            if (options.has("factory-reset"))
                config->setValue("factoryResetOnLoad", options.get("factory-reset") == "on");
            if (options.has("fast-boot"))
                config->setValue("fastBoot", options.get("fast-boot") == "on");
            if (options.has("pcm-card"))
                config->setValue("pcmCardPath", launchFile(options.get("pcm-card")).getFullPathName());
            if (options.has("gain"))
                config->setValue("outputGain", options.number("gain", 1));
            if (options.has("virtual-ports"))
                config->setValue("portMidiEnabled", options.get("virtual-ports") == "on");
            if (options.has("virtual-port-name"))
                config->setValue("virtualPortName", juce::String(options.get("virtual-port-name")));
            return prepareDeviceSetup(options, *config);
        }

        Processor* openDevices(juce::XmlElement& requestedAudio)
        {
            standaloneLaunch = &options;
            standaloneConfig = config.get();
            // Construct the stock holder without endpoints. It normally opens saved
            // devices with default fallback before the editor exists. Open exactly the
            // requested setup ourselves, with fallback disabled for explicit options.
            juce::XmlElement emptyAudio("DEVICESETUP");
            config->setValue("audioSetup", &emptyAudio);
            window = std::make_unique<juce::StandaloneFilterWindow>(getApplicationName(),
                juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
                config.get(), false);
            auto* holder = window->getPluginHolder();
            const bool audioOverrides = options.has("audio-backend") || options.has("audio-device") ||
                options.has("sample-rate") || options.has("buffer-size") || options.has("output-channels");
            config->setValue("audioSetup", &requestedAudio);
            const auto error = holder->deviceManager.initialise(0, 2, &requestedAudio, !audioOverrides);
            if (error.isNotEmpty())
                throw std::runtime_error("Cannot open requested audio setup: " + error.toStdString());
            if (auto* device = holder->deviceManager.getCurrentAudioDevice())
            {
                if (options.has("sample-rate") && device->getCurrentSampleRate() != options.number("sample-rate", 0))
                    throw std::runtime_error("Audio device did not accept --sample-rate.");
                if (options.has("buffer-size") &&
                    device->getCurrentBufferSizeSamples() != options.number("buffer-size", 0))
                    throw std::runtime_error("Audio device did not accept --buffer-size.");
                if (options.has("output-channels"))
                {
                    juce::BigInteger expected;
                    expected.parseString(requestedAudio.getStringAttribute("audioDeviceOutChans"), 2);
                    if (device->getActiveOutputChannels() != expected)
                        throw std::runtime_error("Audio device did not accept --output-channels.");
                }
            }
            else if (options.has("sample-rate") || options.has("buffer-size") || options.has("output-channels"))
                throw std::runtime_error("An audio output device is required for rate, buffer and channel overrides.");
            if (!options.midiInputs.empty())
                for (auto* input : requestedAudio.getChildWithTagNameIterator("MIDIINPUT"))
                    if (!holder->deviceManager.isMidiInputDeviceEnabled(input->getStringAttribute("identifier")))
                        throw std::runtime_error("Cannot open MIDI input: " +
                                                 input->getStringAttribute("name").toStdString());
            if (options.has("midi-out") && options.get("midi-out") != "none" &&
                !holder->deviceManager.getDefaultMidiOutput())
                throw std::runtime_error("Cannot open requested MIDI output.");
            auto* processor = dynamic_cast<Processor*>(holder->processor.get());
            if (!processor)
                throw std::runtime_error("Unable to create player.");
            audioRouting =
                std::make_unique<jucePlayer::AudioRouting>(holder->deviceManager, *config, [processor](bool reverse)
                                                           { processor->setReverseOutputChannels(reverse); });
            // Each MIDI input plays the part groups chosen for it. The stock player merges every
            // input into one stream, so it stops listening to them.
            midiInputRouting = std::make_unique<jucePlayer::MidiInputRouting>(
                holder->deviceManager, *config, [processor](const juce::MidiMessage& message, const uint8_t groups)
                { processor->addLiveMidi(message, groups); }, 4);
            holder->deviceManager.removeMidiInputDeviceCallback({}, &holder->player);
            processor->setRouting(audioRouting.get(), midiInputRouting.get());
            if (options.has("output-channels"))
            {
                const auto [left, right] = parseOutputChannels(options.get("output-channels"));
                const auto routeError = audioRouting->setChannels(left, right);
                if (routeError.isNotEmpty())
                    throw std::runtime_error(routeError.toStdString());
            }
            else
                audioRouting->restoreChannels();
            if (options.has("device") && !processor->hasValidRom())
                throw std::runtime_error(emu88Lib::RomLoader::scan().describeRequirements(
                    emu88Lib::RomLoader::toRomDevice(processor->deviceModel())));
            return processor;
        }

        void timerCallback() override
        {
            stopTimer();
            if (window)
                if (auto* processor = dynamic_cast<Processor*>(window->getPluginHolder()->processor.get()))
                    processor->midiPlayer().play(0);
        }
        bool startupSucceeded = false;
        juce::File saveTarget;
        LaunchOptions options;
        std::unique_ptr<juce::TemporaryFile> temporary;
        std::unique_ptr<juce::PropertiesFile> config;
        std::unique_ptr<juce::StandaloneFilterWindow> window;
        std::unique_ptr<jucePlayer::AudioRouting> audioRouting;
        std::unique_ptr<jucePlayer::MidiInputRouting> midiInputRouting;
    };
} // namespace emu88Player

JUCE_CREATE_APPLICATION_DEFINE(emu88Player::StandaloneApplication)

#include "audioRouting.h"

namespace jucePlayer
{
    namespace
    {
        juce::XmlElement* findRoute(juce::XmlElement& routes, juce::AudioDeviceManager& manager)
        {
            const auto backend = manager.getCurrentAudioDeviceType();
            const auto device = manager.getAudioDeviceSetup().outputDeviceName;
            for (auto* route : routes.getChildIterator())
                if (route->getStringAttribute("backend") == backend && route->getStringAttribute("device") == device)
                    return route;
            return nullptr;
        }
    } // namespace

    AudioRouting::AudioRouting(juce::AudioDeviceManager& deviceManager, juce::PropertiesFile& settings,
                               std::function<void(bool)> reverseChannels) :
        manager(deviceManager), config(settings), reverse(std::move(reverseChannels))
    {
        startTimer(500);
    }

    AudioRouting::~AudioRouting() { stopTimer(); }
    bool AudioRouting::supportsSystem(const juce::String& backend)
    {
        // WASAPI keeps the current default endpoint first in its refreshed list.
        // ALSA's "default" PCM and ASIO driver selection do not expose this policy.
        return backend == "CoreAudio" || backend.startsWith("Windows Audio");
    }

    juce::String AudioRouting::systemOutput(juce::AudioIODeviceType& deviceType)
    {
        deviceType.scanForDevices();
        const auto names = deviceType.getDeviceNames(false);
        const auto index = deviceType.getDefaultDeviceIndex(false);
        return index >= 0 && index < names.size() ? names[index] : juce::String{};
    }

    bool AudioRouting::followsSystem(const juce::PropertySet& settings)
    {
        return settings.getBoolValue("audioFollowSystem", true);
    }
    bool AudioRouting::followsSystem() const { return followsSystem(config); }
    void AudioRouting::disableSystem() { config.setValue("audioFollowSystem", false); }
    void AudioRouting::persist()
    {
        if (auto xml = manager.createStateXml())
            config.setValue("audioSetup", xml.get());
        config.saveIfNeeded();
    }

    juce::String AudioRouting::selectOutput(const juce::String& name, const bool system)
    {
        auto* deviceType = manager.getCurrentDeviceTypeObject();
        if (system && (!deviceType || !supportsSystem(manager.getCurrentAudioDeviceType())))
            return "This backend does not expose system-output following.";
        const auto destination = system ? systemOutput(*deviceType) : name;
        const auto error = selectAudioOutputDevice(manager, destination);
        if (error.isNotEmpty())
            return error;
        config.setValue("audioFollowSystem", system);
        config.setValue("audioOutputDisabled", !system && name.isEmpty());
        restoreChannels();
        persist();
        return {};
    }

    std::pair<int, int> AudioRouting::channels() const
    {
        const auto mask = manager.getAudioDeviceSetup().outputChannels;
        const int first = mask.findNextSetBit(0), second = first < 0 ? -1 : mask.findNextSetBit(first + 1);
        return reversed ? std::make_pair(second, first) : std::make_pair(first, second);
    }

    juce::String AudioRouting::setChannels(const int left, const int right)
    {
        auto* device = manager.getCurrentAudioDevice();
        if (!device)
            return "Select an audio output first.";
        const auto names = device->getOutputChannelNames();
        if (left < 0 || right < 0 || left >= names.size() || right >= names.size() || left == right)
            return "Choose two different output channels.";
        auto setup = manager.getAudioDeviceSetup();
        const auto previous = setup;
        setup.outputChannels.clear();
        setup.outputChannels.setBit(left);
        setup.outputChannels.setBit(right);
        setup.useDefaultOutputChannels = false;
        const auto error = manager.setAudioDeviceSetup(setup, true);
        if (error.isNotEmpty())
        {
            manager.setAudioDeviceSetup(previous, true);
            return error;
        }
        if (!manager.getCurrentAudioDevice() ||
            manager.getCurrentAudioDevice()->getActiveOutputChannels() != setup.outputChannels)
        {
            manager.setAudioDeviceSetup(previous, true);
            return "The driver did not accept the selected output channels.";
        }
        reversed = left > right;
        reverse(reversed);
        routingWarning.clear();
        auto routes = config.getXmlValue("audioChannelRoutes");
        if (!routes)
            routes = std::make_unique<juce::XmlElement>("ROUTES");
        auto* route = findRoute(*routes, manager);
        if (!route)
            route = routes->createNewChildElement("ROUTE");
        route->setAttribute("backend", manager.getCurrentAudioDeviceType());
        route->setAttribute("device", setup.outputDeviceName);
        route->setAttribute("left", left);
        route->setAttribute("right", right);
        route->setAttribute("leftName", names[left]);
        route->setAttribute("rightName", names[right]);
        config.setValue("audioChannelRoutes", routes.get());
        persist();
        return {};
    }

    void AudioRouting::restoreChannels()
    {
        routingWarning.clear();
        reversed = false;
        reverse(false);
        auto* device = manager.getCurrentAudioDevice();
        if (!device)
            return;
        auto routes = config.getXmlValue("audioChannelRoutes");
        if (!routes)
            return;
        auto* route = findRoute(*routes, manager);
        if (!route)
            return;
        const auto names = device->getOutputChannelNames();
        auto resolve = [&](const char* key, const char* nameKey)
        {
            const auto index = route->getIntAttribute(key, -1);
            const auto name = route->getStringAttribute(nameKey);
            if (index >= 0 && index < names.size() && names[index] == name)
                return index;
            return names.indexOf(name);
        };
        if (setChannels(resolve("left", "leftName"), resolve("right", "rightName")).isEmpty())
            return;
        auto setup = manager.getAudioDeviceSetup();
        setup.useDefaultOutputChannels = true;
        const auto error = manager.setAudioDeviceSetup(setup, true);
        routingWarning = "Saved output mapping unavailable: left \"" + route->getStringAttribute("leftName") +
            "\", right \"" + route->getStringAttribute("rightName") + "\". " +
            (error.isEmpty() ? juce::String("Using the device's default outputs. ")
                             : "Could not restore default outputs: " + error + ". ") +
            "Choose replacement output channels above.";
        juce::Logger::writeToLog(routingWarning);
    }

    void AudioRouting::timerCallback() { refreshSystemOutput(); }
    void AudioRouting::refreshSystemOutput()
    {
        if (!followsSystem())
            return;
        auto* deviceType = manager.getCurrentDeviceTypeObject();
        if (!deviceType || !supportsSystem(manager.getCurrentAudioDeviceType()))
            return;
        const auto destination = systemOutput(*deviceType);
        if (destination == manager.getAudioDeviceSetup().outputDeviceName &&
            (destination.isEmpty() || manager.getCurrentAudioDevice()))
            return;
        const auto error = selectOutput(destination, true);
        if (error.isNotEmpty() && error != lastError)
            juce::Logger::writeToLog("Unable to follow system audio output: " + error);
        lastError = error;
        const auto changed = onDeviceChanged;
        if (error.isEmpty() && changed)
            changed();
    }
    juce::String selectAudioDeviceType(juce::AudioDeviceManager& _manager, const juce::String& _type,
                                       const int _outputChannels)
    {
        if (_type == _manager.getCurrentAudioDeviceType())
            return {};
        if (_type != "ASIO")
        {
            _manager.setCurrentAudioDeviceType(_type, true);
            return {};
        }

        // setCurrentAudioDeviceType opens a default driver (preferentially ASIO4ALL)
        // before the user can choose one. Loading an unavailable/broken driver can
        // crash inside its DLL. Restore an explicitly empty audio setup instead;
        // JUCE scans driver names but creates no device until the user selects it.
        // Keep the MIDI state, including remembered disconnected inputs.
        auto state = _manager.createStateXml();
        if (!state)
            state = std::make_unique<juce::XmlElement>("DEVICESETUP");
        state->setAttribute("deviceType", _type);
        for (const auto* attribute :
             {"audioDeviceName", "audioInputDeviceName", "audioOutputDeviceName", "audioDeviceRate",
              "audioDeviceBufferSize", "audioDeviceInChans", "audioDeviceOutChans"})
            state->removeAttribute(attribute);
        return _manager.initialise(0, _outputChannels, state.get(), false);
    }

    juce::String selectAudioOutputDevice(juce::AudioDeviceManager& _manager, const juce::String& _name)
    {
        auto setup = _manager.getAudioDeviceSetup();
        if (setup.outputDeviceName != _name)
        {
            // Let the new driver choose supported defaults instead of inheriting
            // the previous backend's buffer size, sample rate and channel mask.
            setup.sampleRate = 0;
            setup.bufferSize = 0;
            setup.useDefaultOutputChannels = true;
        }
        setup.outputDeviceName = _name;
        setup.inputDeviceName.clear();
        setup.useDefaultInputChannels = true;
        return _manager.setAudioDeviceSetup(setup, true);
    }

    bool showAudioDeviceControlPanel(juce::AudioDeviceManager& _manager)
    {
        auto* device = _manager.getCurrentAudioDevice();
        if (!device || !device->hasControlPanel() || !device->showControlPanel())
            return false;

        // closeAudioDevice deletes the device. Follow JUCE's device selector:
        // show the panel while it is alive, then reopen if its settings changed.
        _manager.closeAudioDevice();
        _manager.restartLastAudioDevice();
        return true;
    }
} // namespace jucePlayer

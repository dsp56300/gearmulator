#include "jucePlayerLib/audioRouting.h"
#include "baseLib/os.h"
#include <iostream>
#include <stdexcept>
#include "juce_events/juce_events.h"

namespace
{
    void check(bool ok, const char* message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }
    struct FakeDevice final : juce::AudioIODevice
    {
        FakeDevice(const juce::String& name) : AudioIODevice(name, "CoreAudio") {}
        juce::StringArray getOutputChannelNames() override
        {
            return getName() == "USB" ? juce::StringArray{"Main L", "Main R", "Loopback L", "Loopback R"}
                                      : juce::StringArray{"Left", "Right"};
        }
        juce::StringArray getInputChannelNames() override { return {}; }
        juce::Array<double> getAvailableSampleRates() override { return {44100, 48000}; }
        juce::Array<int> getAvailableBufferSizes() override { return {128, 512}; }
        int getDefaultBufferSize() override { return 512; }
        juce::String open(const juce::BigInteger&, const juce::BigInteger& channels, double sampleRate,
                          int size) override
        {
            mask = channels;
            rate = sampleRate;
            buffer = size;
            opened = true;
            return {};
        }
        void close() override
        {
            stop();
            opened = false;
        }
        bool isOpen() override { return opened; }
        void start(juce::AudioIODeviceCallback* callback) override
        {
            cb = callback;
            if (cb)
                cb->audioDeviceAboutToStart(this);
        }
        void stop() override
        {
            if (cb)
                cb->audioDeviceStopped();
            cb = nullptr;
        }
        bool isPlaying() override { return cb != nullptr; }
        juce::String getLastError() override { return {}; }
        int getCurrentBufferSizeSamples() override { return buffer; }
        double getCurrentSampleRate() override { return rate; }
        int getCurrentBitDepth() override { return 24; }
        juce::BigInteger getActiveOutputChannels() const override { return mask; }
        juce::BigInteger getActiveInputChannels() const override { return {}; }
        int getOutputLatencyInSamples() override { return 0; }
        int getInputLatencyInSamples() override { return 0; }
        juce::BigInteger mask;
        juce::AudioIODeviceCallback* cb = nullptr;
        bool opened = false;
        double rate = 48000;
        int buffer = 512;
    };
    struct FakeType final : juce::AudioIODeviceType
    {
        FakeType() : AudioIODeviceType("CoreAudio") {}
        void scanForDevices() override {}
        juce::StringArray getDeviceNames(bool input) const override { return input ? juce::StringArray{} : names; }
        int getDefaultDeviceIndex(bool) const override { return defaultIndex; }
        int getIndexOfDevice(juce::AudioIODevice* d, bool) const override { return names.indexOf(d->getName()); }
        bool hasSeparateInputsAndOutputs() const override { return true; }
        juce::AudioIODevice* createDevice(const juce::String& output, const juce::String&) override
        {
            ++opens;
            return names.contains(output) ? new FakeDevice(output) : nullptr;
        }
        juce::StringArray names{"Speakers", "Headphones", "USB"};
        int defaultIndex = 0, opens = 0;
    };
} // namespace
int main()
{
    baseLib::disableErrorDialogs();

    try
    {
        juce::ScopedJuceInitialiser_GUI initialise;
        juce::TemporaryFile temporary(".xml");
        juce::PropertiesFile::Options options;
        options.millisecondsBeforeSaving = -1;
        juce::PropertiesFile config(temporary.getFile(), options);
        juce::AudioDeviceManager manager;
        const auto& available = manager.getAvailableDeviceTypes();
        const std::vector<juce::AudioIODeviceType*> types(available.begin(), available.end());
        for (auto* type : types)
            manager.removeAudioDeviceType(type);
        auto* type = new FakeType;
        manager.addAudioDeviceType(std::unique_ptr<juce::AudioIODeviceType>(type));
        juce::XmlElement initial("DEVICESETUP");
        initial.setAttribute("deviceType", "CoreAudio");
        initial.setAttribute("audioOutputDeviceName", "Speakers");
        check(manager.initialise(0, 2, &initial, false).isEmpty(), "Initialise fake audio");
        bool reverse = false;
        jucePlayer::AudioRouting routing(manager, config, [&](bool value) { reverse = value; });
        check(routing.followsSystem() && jucePlayer::AudioRouting::followsSystem(juce::PropertySet{}),
              "A config without a saved choice follows the system");
        check(routing.selectOutput({}).isEmpty(), "Select no output");
        check(!routing.followsSystem(), "Choosing no output is saved as a choice");
        check(config.getBoolValue("audioOutputDisabled", false), "Remember explicit no-output selection");
        check(routing.selectOutput({}, true).isEmpty(), "Enable system policy");
        check(!config.getBoolValue("audioOutputDisabled", true), "System output clears explicit disable");
        type->defaultIndex = 1;
        routing.refreshSystemOutput();
        check(manager.getAudioDeviceSetup().outputDeviceName == "Headphones", "Follow headphones");
        type->defaultIndex = 2;
        routing.refreshSystemOutput();
        check(manager.getAudioDeviceSetup().outputDeviceName == "USB", "Follow USB");
        check(routing.setChannels(3, 2).isEmpty() && reverse, "Reverse loopback stereo channels");
        check(routing.channels() == std::make_pair(3, 2), "Logical routing indices");
        type->defaultIndex = 0;
        routing.refreshSystemOutput();
        check(!reverse && routing.channels() == std::make_pair(0, 1), "Default stereo on smaller device");
        type->defaultIndex = 2;
        routing.refreshSystemOutput();
        check(reverse && routing.channels() == std::make_pair(3, 2), "Restore USB routing");
        check(routing.setChannels(1, 1).isNotEmpty(), "Reject duplicate channels");
        check(routing.setChannels(9, 0).isNotEmpty(), "Reject nonexistent channels");
        const auto opens = type->opens;
        routing.refreshSystemOutput();
        check(type->opens == opens, "Stable default does not reopen device");
        check(routing.selectOutput("Headphones").isEmpty() && !routing.followsSystem(), "Pin headphones");
        type->defaultIndex = 0;
        routing.refreshSystemOutput();
        check(manager.getAudioDeviceSetup().outputDeviceName == "Headphones", "Pinned device ignores system changes");
        routing.selectOutput({}, true);
        type->names.clear();
        type->defaultIndex = -1;
        routing.refreshSystemOutput();
        check(!manager.getCurrentAudioDevice(), "No default closes audio");
        type->names = {"Speakers", "Headphones", "USB"};
        type->defaultIndex = 2;
        routing.refreshSystemOutput();
        check(manager.getAudioDeviceSetup().outputDeviceName == "USB" && reverse, "Default returns with saved routing");
        {
            juce::PropertiesFile reloaded(temporary.getFile(), options);
            bool restoredReverse = false;
            jucePlayer::AudioRouting restored(manager, reloaded, [&](bool value) { restoredReverse = value; });
            restored.restoreChannels();
            check(restoredReverse && restored.channels() == std::make_pair(3, 2), "Routing survives settings reload");
            auto routes = reloaded.getXmlValue("audioChannelRoutes");
            for (auto* route : routes->getChildIterator())
                route->setAttribute("leftName", "Removed channel");
            reloaded.setValue("audioChannelRoutes", routes.get());
            restored.restoreChannels();
            check(!restoredReverse && restored.channels() == std::make_pair(0, 1),
                  "Changed channel layout uses default outputs");
            check(restored.channelWarning().contains("Removed channel") &&
                      restored.channelWarning().contains("default outputs"),
                  "Unavailable mapping identifies the missing channel and fallback");
            check(reloaded.getXmlValue("audioChannelRoutes")->toString().contains("Removed channel"),
                  "Fallback preserves the saved mapping for recovery");
            check(restored.setChannels(0, 1).isEmpty() && restored.channelWarning().isEmpty(),
                  "Choosing a replacement clears the warning");
            auto duplicatedRoutes = reloaded.getXmlValue("audioChannelRoutes");
            duplicatedRoutes->addChildElement(new juce::XmlElement(*duplicatedRoutes->getFirstChildElement()));
            reloaded.setValue("audioChannelRoutes", duplicatedRoutes.get());
            check(restored.setChannels(1, 0).isEmpty(),
                  "Channel selection accepts a config with duplicate route entries");
            restored.restoreChannels();
            check(restored.channels() == std::make_pair(1, 0), "Save and restore agree on the first matching route");
        }
        check(!jucePlayer::AudioRouting::supportsSystem("ASIO") && !jucePlayer::AudioRouting::supportsSystem("ALSA"),
              "Unsupported default policies excluded");
        std::cout << "Audio routing and system-output transitions passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

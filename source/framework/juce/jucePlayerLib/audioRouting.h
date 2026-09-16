#pragma once
#include <functional>
#include "juce_audio_devices/juce_audio_devices.h"
#include "juce_data_structures/juce_data_structures.h"

namespace jucePlayer
{
    class AudioRouting final : private juce::Timer
    {
    public:
        AudioRouting(juce::AudioDeviceManager& manager, juce::PropertiesFile& config,
                     std::function<void(bool)> reverse);
        ~AudioRouting() override;
        static bool supportsSystem(const juce::String& backend);
        static juce::String systemOutput(juce::AudioIODeviceType& type);
        // On until the user picks an output: a config without a saved choice follows the system.
        static bool followsSystem(const juce::PropertySet& config);
        bool followsSystem() const;
        juce::String selectOutput(const juce::String& name, bool system = false);
        juce::String setChannels(int left, int right);
        std::pair<int, int> channels() const;
        const juce::String& channelWarning() const { return routingWarning; }
        void restoreChannels();
        void disableSystem();
        void refreshSystemOutput();
        std::function<void()> onDeviceChanged;

    private:
        void timerCallback() override;
        void persist();
        juce::AudioDeviceManager& manager;
        juce::PropertiesFile& config;
        std::function<void(bool)> reverse;
        bool reversed = false;
        juce::String lastError;
        juce::String routingWarning;
    };
    juce::String selectAudioDeviceType(juce::AudioDeviceManager& _manager, const juce::String& _type,
                                       const int _outputChannels);
    juce::String selectAudioOutputDevice(juce::AudioDeviceManager& _manager, const juce::String& _name);
    bool showAudioDeviceControlPanel(juce::AudioDeviceManager& _manager);

} // namespace jucePlayer

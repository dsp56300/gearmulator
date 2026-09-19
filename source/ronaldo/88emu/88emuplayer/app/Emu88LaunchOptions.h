#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>
#include "88lib/deviceModel.h"
#include "jucePlayerLib/midiPlayer.h"
#include "juce_core/juce_core.h"
#include "juce_data_structures/juce_data_structures.h"

namespace emu88Player
{
    struct MidiInputOption
    {
        std::string value;
        uint8_t groups = 0;
    };

    struct LaunchOptions
    {
        std::map<std::string, std::string> values;
        std::vector<std::string> files;
        std::vector<MidiInputOption> midiInputs;
        bool has(const std::string& key) const { return values.count(key) != 0; }
        std::string get(const std::string& key, const std::string& fallback = {}) const;
        double number(const std::string& key, double fallback) const;
        bool sessionOverrides() const;
        static LaunchOptions parse(const juce::StringArray& args, bool cli);
        static std::string help(bool cli);
    };

    const char* deviceId(emu88Lib::DeviceModel model);
    emu88Lib::DeviceModel parseDevice(const std::string& id);
    jucePlayer::MidiPlayer::ResetMode parseReset(const std::string& name);
    std::pair<int, int> parseOutputChannels(const std::string& value);
    juce::File launchFile(const std::string& path);
    // The PCM card image in the file. Throws if the file is missing or not a card.
    std::vector<uint8_t> loadPcmCard(const juce::File& file);
    // The --pcm-card image, or empty without the option. Throws as above.
    std::vector<uint8_t> loadPcmCard(const LaunchOptions& options);
    void listDevices();
    std::string defaultDataFolder();
    void configureRomSearchPaths(const LaunchOptions& options);

    // Set by the standalone entry point before JUCE creates the processor.
    inline const LaunchOptions* standaloneLaunch = nullptr;
    inline juce::PropertiesFile* standaloneConfig = nullptr;
} // namespace emu88Player

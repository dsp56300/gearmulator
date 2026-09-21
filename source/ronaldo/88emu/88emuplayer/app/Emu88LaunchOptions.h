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
    void listDevices(const LaunchOptions& options);
    std::string defaultDataFolder();
    // The folder searched for ROMs, subfolders included: --rom-dir, else the player's data folder.
    juce::File romSearchFolder(const LaunchOptions& options);
    void configureRomSearchPaths(const LaunchOptions& options);
    // Why the ROM search folder cannot be looked into, and what to do about it; empty when it can.
    // Every ROM would otherwise just read as missing.
    std::string romFolderAccessError(const LaunchOptions& options);

    // True when the macOS privacy settings, not the file or folder itself, keep the player from reading
    // path. Documents, Desktop, Downloads and removable or network volumes need the user's permission,
    // and a process without it is refused with EPERM. Always false on other platforms.
    bool blockedByPrivacySettings(const std::string& path);
    // What to do when the macOS privacy settings keep the player from reading any of paths, said once
    // for all of them; empty when they do not.
    std::string privacySettingsHint(const std::vector<std::string>& paths);

    // Set by the standalone entry point before JUCE creates the processor.
    inline const LaunchOptions* standaloneLaunch = nullptr;
    inline juce::PropertiesFile* standaloneConfig = nullptr;
} // namespace emu88Player

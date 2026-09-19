#pragma once

#include "jucePlayerLib/midiPlayer.h"

#include "juce_core/juce_core.h"

#include <string>
#include <vector>

namespace emu88Player::playlist
{
    inline constexpr auto fileFilter = "*.m3u;*.m3u8";

    bool isSupported(const std::string& _path);
    juce::File defaultFile();
    bool read(const juce::File& _file, std::vector<std::string>& _paths, std::string& _error);
    bool write(const juce::File& _file, const std::vector<jucePlayer::MidiPlayer::Entry>& _entries,
               std::string& _error);
} // namespace emu88Player::playlist

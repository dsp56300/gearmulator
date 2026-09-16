#pragma once

#include "synthLib/midi/midiFile.h"

namespace jucePlayer::midiFile
{
    inline constexpr auto fileFilter = "*.mid;*.midi;*.rcp;*.r36;*.g36";
    bool isSupported(const std::string& _path);
    bool read(const std::string& _path, std::vector<synthLib::midi::Event>& _events, std::string& _error);
} // namespace jucePlayer::midiFile

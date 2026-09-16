#pragma once

#include "synthLib/midiTypes.h"

namespace synthLib::midi
{
    enum class ResetMode : uint8_t
    {
        Off,
        Gm,
        Gs,
        Mt32
    };

    void appendSongReset(std::vector<SMidiEvent>& events, ResetMode mode, uint8_t portCount, uint32_t offset);
    void appendGsMt32Arrangement(std::vector<SMidiEvent>& events, uint8_t portCount, uint32_t offset);
} // namespace synthLib::midi

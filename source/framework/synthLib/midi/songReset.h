#pragma once

#include "synthLib/midiTypes.h"

namespace synthLib::midi
{
    enum class ResetMode : uint8_t
    {
        Off,
        Gm,
        Gs,
        Mt32,
        // Last rather than next to Gm: players save the mode as its number.
        Gm2
    };

    // Whether _value names a ResetMode, for one read back from saved settings.
    constexpr bool isResetModeValue(const int _value)
    {
        return _value >= 0 && _value <= static_cast<int>(ResetMode::Gm2);
    }

    void appendSongReset(std::vector<SMidiEvent>& events, ResetMode mode, uint8_t portCount, uint32_t offset);
    void appendGsMt32Arrangement(std::vector<SMidiEvent>& events, uint8_t portCount, uint32_t offset);
} // namespace synthLib::midi

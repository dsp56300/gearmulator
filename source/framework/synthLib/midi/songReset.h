#pragma once

#include "synthLib/midiTypes.h"

#include <array>

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

    // What the device being played answers to. The Sound Canvas family and the modules
    // built on it take the GM, GM2 and GS System On messages the modes name. Roland's LA
    // and CM boards - the MT-32, CM-32L, CM-32P and CM-64 - predate them all and know only
    // their own "all parameters reset", a data set to address 7F 00 00, so on those every
    // mode but Off sends that, and the MT-32 arrangement of the GS map is meaningless.
    enum class ResetTarget : uint8_t
    {
        GsModule,
        RolandLaModule
    };

    // The LA and CM boards' reset: model ID 16H, device ID 10H (unit 17), address 7F 00 00.
    constexpr std::array<uint8_t, 11> kRolandLaResetSysex = {0xf0, 0x41, 0x10, 0x16, 0x12, 0x7f, 0x00,
                                                            0x00, 0x00, 0x01, 0xf7};
    constexpr std::array<uint8_t, 11> kGsResetSysex = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00,
                                                      0x7f, 0x00, 0x41, 0xf7};

    void appendSongReset(std::vector<SMidiEvent>& events, ResetMode mode, uint8_t portCount, uint32_t offset,
                         ResetTarget target = ResetTarget::GsModule);
    void appendGsMt32Arrangement(std::vector<SMidiEvent>& events, uint8_t portCount, uint32_t offset);
} // namespace synthLib::midi

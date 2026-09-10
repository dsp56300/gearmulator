#pragma once

#include "dsp56300_fixed.h"

#include <cstdint>

namespace nmm::native
{
// Result of the resident P:$1d1..$1d8 stereo mix. The words are the exact
// 24-bit values written to Y:(r1)+ and then sent through the codec path. The
// signed fields apply the AD1865's final 18-bit two's-complement decoder.
struct DacStereo
{
    Word24 leftWord = 0;
    Word24 rightWord = 0;
    std::int32_t leftSigned18 = 0;
    std::int32_t rightSigned18 = 0;
};

// Decode the final 18 serial bits used by nmm::decodeDac without pulling the
// emulator codec header into the native Teensy core.
constexpr std::int32_t decodeDacSigned18(Word24 serialWord) noexcept
{
    const auto bits = serialWord & 0x3ffffu;
    return static_cast<std::int32_t>(bits ^ 0x20000u) - 0x20000;
}

// Execute the resident master/output prefix in isolation. inputFourWords is
// an explicit graph output buffer: [0,2] contribute to left and [1,3] to
// right. This function does not model X:$04/X:$05 buffer ownership or the
// one-frame ESSI queue latency.
DacStereo runResidentDac(const Word24* inputFourWords,
                         Word24 masterCoefficient,
                         Word24 dcOffset) noexcept;
}

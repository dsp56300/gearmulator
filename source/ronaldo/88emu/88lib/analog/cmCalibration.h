#pragma once

#include <array>

namespace emu88Lib::cmCalibration
{
    struct Channel
    {
        // First four fields multiply nominal schematic section frequency/Q.
        double firstFrequency, firstQ, secondFrequency, secondQ, bandwidthHz;
    };

    // Effective section tolerances and bandwidth, not identified component values.
    // Fit only dry saw/organ/strings; square, sax, low-master and reverb are holdouts.
    constexpr std::array<Channel, 2> La{{
        {1.07396, .94645, 1.04635, .93201, 53781.0},
        {1.10196, .92515, 1.01933, 1.00794, 46964.0},
    }};
    constexpr std::array<Channel, 2> Pcm{{
        {1.02215, .95795, .95515, .98427, 69300.0},
        {1.01598, .95843, 1.02563, 1.00849, 63601.0},
    }};

    // Preserve the schematic LA-left normalization; capture dBFS is not volts.
    constexpr double PcmRelativeGain = 1.96970;
    constexpr float LaRight = 1.01565f;
    constexpr float PcmRight = .99604f;
}

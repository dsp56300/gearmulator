#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace emu88Lib::cmVca
{
    // 2026-09-22 reference, master sweeps relative to each channel's master 100.
    // Shared-channel robust fit; the tiny residual L/R law differences are not
    // separate control-voltage measurements. PCM master 0/1 is noise-limited.
    // The M5207L01 is approximately linear above a small control offset. Keeping
    // the RC voltage separate from gain also makes the mute threshold act AFTER
    // smoothing, rather than inventing an RC on the clipped audio gain.
    constexpr float LaOffsetCounts = 3.36f;
    constexpr float PcmOffsetCounts = 3.79f;
    constexpr float TimeConstant = 82e3f * .1e-6f;

    inline float gain(const float control, const float offsetCounts)
    {
        const float offset = offsetCounts / 255.0f;
        return std::clamp((control - offset) / (1.0f - offset), 0.0f, 1.0f);
    }

    inline float process(float& control, const uint8_t duty, const float offsetCounts)
    {
        const float target = 1.0f - static_cast<float>(duty) / 255.0f;
        const float alpha = 1.0f - std::exp(-1.0f / (32000.0f * TimeConstant));
        control += (target - control) * alpha;
        return gain(control, offsetCounts);
    }
}

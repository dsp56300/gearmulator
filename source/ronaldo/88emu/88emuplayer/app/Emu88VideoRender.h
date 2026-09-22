#pragma once

#include "88emuplayer/app/Emu88LaunchOptions.h"

namespace emu88Player
{
    // Renders the playlist's first entry to a video file, showing the player's own UI alongside the
    // audio it produced. Offline and deterministic: the board is advanced one video frame's worth of
    // samples at a time and the UI is drawn from the display state that leaves, so picture and sound
    // line up exactly however long the render takes. Returns the process exit code.
    int renderVideo(const LaunchOptions& _options);
}

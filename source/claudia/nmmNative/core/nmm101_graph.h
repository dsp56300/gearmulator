#pragma once
#include "nmm101_kernels.h"
#include <array>

namespace nmm::native {
// Fixed compiled 101 graph. Initialization is supplied by a locally generated
// emulator fixture; this class neither boots firmware nor compiles patches.
// Control and sample calls are explicit so a reference harness can reproduce
// the measured scheduling phase instead of assuming a reset phase.
class Graph101 {
public:
    static constexpr std::size_t words = 0x800;
    Graph101() noexcept;
    Graph101(const Graph101&) = delete;
    Graph101& operator=(const Graph101&) = delete;
    void initialize(const Word24* initialX, const Word24* initialY) noexcept;
    void setVoiceWords(Word24 pitch, Word24 velocity, Word24 gate) noexcept;
    // Call after changing fixed patch coefficients or shared lookup tables
    // through the diagnostic memory accessor. MIDI transport does not need it.
    void invalidateCoefficients() noexcept;
    void control() noexcept;
    StereoWords sample() noexcept;
    GraphMemory& memory() noexcept { return memory_; }
    const ModuleCursor& cursor() const noexcept { return cursor_; }
private:
    std::array<Word24, words> x_{}, y_{};
    GraphMemory memory_;
    ModuleCursor cursor_;
    ModuleCursor setupCache_{}, controlCache_{};
    Word24 setupPitch_ = 0, setupSync_ = 0, setupOutput_ = 0;
    Word24 controlPitch_ = 0, controlVelocity_ = 0, controlOutput_ = 0;
    bool setupValid_ = false, controlValid_ = false;
};
}

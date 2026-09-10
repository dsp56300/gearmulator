#include "nmm101_graph.h"
#include "nmm101_envelope.h"
#include "nmm101_filter_control.h"

namespace nmm::native {
namespace {
void restoreCoefficientRegisters(ModuleCursor& to, const ModuleCursor& from) noexcept {
    to.r2=from.r2; to.r3=from.r3; to.r4=from.r4; to.r5=from.r5;
    to.a=from.a; to.b=from.b;
    to.x0=from.x0; to.x1=from.x1; to.y0=from.y0; to.y1=from.y1;
}
}
Graph101::Graph101() noexcept
    : memory_{x_.data(), y_.data(), words, words, false} {
    cursor_.memory = &memory_;
}

void Graph101::initialize(const Word24* initialX, const Word24* initialY) noexcept {
    for (std::size_t i = 0; i < words; ++i) {
        x_[i] = mask24(initialX[i]);
        y_[i] = mask24(initialY[i]);
    }
    memory_.fault = false;
    cursor_ = {};
    cursor_.memory = &memory_;
    cursor_.n2 = 0x7bc;
    cursor_.n5 = 0x780;
    invalidateCoefficients();
}

void Graph101::invalidateCoefficients() noexcept {
    setupValid_=false;
    controlValid_=false;
}

void Graph101::setVoiceWords(Word24 pitch, Word24 velocity, Word24 gate) noexcept {
    memory_.writeX(0x0f, pitch);
    memory_.writeX(0x0d, velocity);
    memory_.writeX(0x0e, gate);
}

void Graph101::control() noexcept {
    cursor_.r3 = 0x73;
    cursor_.r4 = 0x6a;
    cursor_.x0 = memory_.readX(0x0f);
    const auto pitch=memory_.readX(0x0f);
    const auto velocity=memory_.readX(0x0d);
    if(!controlValid_ || pitch!=controlPitch_ || velocity!=controlVelocity_) {
        runFilterF92Control(cursor_);
        controlCache_=cursor_;
        controlPitch_=pitch; controlVelocity_=velocity;
        controlOutput_=memory_.readX(cursor_.r5);
        controlValid_=true;
    } else {
        // Fixed 101 filter control has no recursive state. Its coefficient
        // depends on pitch/velocity and immutable patch/LUT words. Preserve
        // the generated write and the envelope's live prefetch boundary.
        restoreCoefficientRegisters(cursor_,controlCache_);
        memory_.writeX(cursor_.r5,controlOutput_);
        cursor_.x0=memory_.readX(cursor_.r3-1);
        cursor_.y0=memory_.readY(cursor_.r4-1);
    }
    runEnvelope20Control(cursor_, true);
}

StereoWords Graph101::sample() noexcept {
    // The resident interrupt prepares a cleared stereo mix buffer before
    // the module graph. Keep that buffer operation outside the kernels.
    const auto output = memory_.readX(4);
    memory_.writeY(output, 0);
    memory_.writeY(output + 1, 0);
    cursor_.r3 = 0x60;
    cursor_.r4 = 0x60;
    const auto pitch=memory_.readX(0x0f);
    const auto sync=memory_.readY(0x62);
    if(!setupValid_ || pitch!=setupPitch_ || sync!=setupSync_) {
        runOscillatorType7Setup(cursor_);
        setupCache_=cursor_;
        setupPitch_=pitch; setupSync_=sync;
        setupOutput_=memory_.readX(0x11);
        setupValid_=true;
    } else {
        // Phase integration remains in the oscillator body. This prefix
        // computes pitch coefficients and consumes/clears the sync word.
        restoreCoefficientRegisters(cursor_,setupCache_);
        memory_.writeX(0x11,setupOutput_);
        memory_.writeY(0x62,0);
    }
    runOscillatorType7(cursor_);
    runCompiledFilterF92(cursor_);
    return runOutput4(cursor_);
}
}

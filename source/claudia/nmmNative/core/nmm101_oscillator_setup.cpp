#include "nmm101_kernels.h"

namespace nmm::native {
void runOscillatorType7Setup(ModuleCursor& c) noexcept {
    auto& m = *c.memory;
    // Exact selected-101 pitch/setup P:1df..1f9, before JMP 281.
    c.x0 = m.readX(0x0f);
    c.x1 = c.xReadInc();
    c.a = mpy(c.x1, c.x0); c.x0 = 0;
    c.a = asl(c.a, 1); c.x1 = c.xReadInc(); c.y1 = c.yReadInc();
    c.a = mac(c.a, c.x1, c.x0); c.x0 = 0;
    c.a = mac(c.a, c.x0, c.y1); c.y1 = c.xReadInc();
    c.a = asl(c.a, 2);
    c.x0 = c.xReadInc(); c.y0 = c.yReadInc();
    c.a = from24(limit24(c.a));
    const unsigned width = (c.y1 >> 12) & 0x3f;
    const unsigned offset = c.y1 & 0x3f;
    const uint64_t mask = width >= 56 ? 0x00ffffffffffffffull : ((uint64_t(1) << width) - 1);
    c.b = offset >= 56 ? 0 : wrap56Raw((uint64_t(c.a) >> offset) & mask);
    c.a = asr(c.a, 17);
    c.r5 = limit24(c.b); c.r2 = limit24(c.a);
    c.x1 = 0;
    c.a = mpy(c.x1, c.y0);
    c.a = add(c.a, from24(c.x0)); c.y0 = 0;
    c.a = asl(c.a, 1);
    // P:1ef's X read is overwritten by P:1f0 before use. For negative
    // pitches its pre-update address falls in peripheral space; the native
    // graph has no such peripheral bus and only needs the AGU update.
    c.r2 = mask24(c.r2 + c.n2);
    c.y1 = m.readY(c.r5); c.r5 = mask24(c.r5 + c.n5);
    c.a = asl(c.a, 1);
    c.x0 = m.readX(c.r2); c.y1 = m.readY(c.r5);
    c.b = mpy(c.x0, c.y1); c.x1 = m.readY(c.r4);
    c.a = asl(c.a, 1); c.x0 = c.xReadInc(); c.yWriteInc(c.y0);
    c.y1 = limit24(c.b);
    c.b = mpy(c.x0, c.y1); c.x0 = 0;
    c.b = asl(c.b, 7);
    c.y1 = c.xReadInc();
    const auto beforeMac = c.b;
    c.b = mac(c.b, c.x0, c.y1); m.writeX(0x11, limit24(beforeMac));
    c.b = mac(c.b, c.x0, c.y1); c.x0 = c.xReadInc(); c.y1 = limit24(c.a);
    c.b = asr(c.b, 2);
    // The body establishes its own branch flags with MPYR then TST B.
}
}

#include "nmm101_kernels.h"

#include <array>
#include <cassert>

using namespace nmm::native;

int main()
{
    GraphMemory memory;
    std::array<Word24, 0x800> x{};
    std::array<Word24, 0x800> y{};
    memory.x = x.data();
    memory.y = y.data();
    memory.xWords = x.size();
    memory.yWords = y.size();
    ModuleCursor cursor;
    cursor.memory = &memory;
    cursor.r3 = 0x100;
    cursor.r4 = 0x104; // The template writes back at r4-4 at the end.

    // A zero coefficient/state region is a useful exact silence vector: no
    // float approximation or denormal handling can accidentally create output.
    x[0x10] = 0x400000;
    assert(runFilterF92(cursor) == 0);

    // Check the public transfer convention used by the final amplifier.
    assert(to24(from24(0x00600000)) == 0x00600000);
    assert(to24(mpy(0x00400000, 0x00400000)) == 0x00200000);

    cursor.r3 = 0x200;
    cursor.r4 = 0x240;
    cursor.b = from24(0x00400000);
    const auto oscillatorWord = finishOscillatorType7(cursor);
    assert(oscillatorWord == 0x00100000);
    assert(x[0x10] == 0x00100000);
    assert(cursor.r3 == 0x201 && cursor.r4 == 0x241);
    return 0;
}

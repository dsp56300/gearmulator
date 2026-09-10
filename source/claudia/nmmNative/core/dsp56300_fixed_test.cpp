#include "dsp56300_fixed.h"

#include <cassert>
#include <cstdint>

using namespace nmm::native;

int main()
{
    static_assert(sign24(0x00ffffff) == -1);
    static_assert(sign24(0x00800000) == -0x800000);
    static_assert(from24(0x00400000) == 0x400000000000ll);
    static_assert(mpy(0x00400000, 0x00400000) == 0x200000000000ll);
    static_assert(to24(mpy(0x00400000, 0x00400000)) == 0x00200000);
    static_assert(mpy(0x00800000, 0x00800000) == 0x800000000000ll);
    static_assert(to24(mpy(0x00800000, 0x00800000)) == 0x00800000);
    static_assert(asr(-3, 1) == -2);
    static_assert(asl(0x100, 4) == 0x1000);
    static_assert(round24(0x7fffff) == 0);
    static_assert(round24(0x800000) == 0);
    static_assert(round24(0x800001) == 0x1000000);

    assert(to24(mac(from24(0x00400000), 0x00400000, 0x00400000)) == 0x00600000);
    assert(mask24(0xdeadbeef) == 0x00adbeef);
    return 0;
}

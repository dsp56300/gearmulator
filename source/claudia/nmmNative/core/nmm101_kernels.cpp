#include "nmm101_kernels.h"

namespace nmm::native
{
static Word24 filterF92(ModuleCursor& c, const std::size_t inputAddress,
                       const bool compiled) noexcept
{
    auto& memory = *c.memory;

    // 092-sample-P.asm, with the generated 101 relocation of x:$0000 to
    // x:$0010.  Parallel moves are written in source order only where their
    // destinations are independent; every ALU operation sees the source
    // register values from the preceding instruction, as on the 56303.
    if (!compiled) {
        c.x0 = c.xReadInc();
        c.y0 = c.yReadInc();
    }
    c.b = mpy(c.y0, c.x0);
    c.a = from24(memory.readX(inputAddress));
    c.b = asl(c.b, 3);
    c.a = sub(c.a, c.b);
    c.y0 = limit24(c.a);

    c.y1 = c.yReadInc();
    c.b = mpy(c.y0, c.y0);
    c.a = mpy(c.y1, c.y0);
    c.x0 = limit24(c.b);
    c.x1 = c.xReadInc();
    c.b = mpy(c.y0, c.x0);
    c.x0 = limit24(c.b);
    c.a = round24(sub(c.a, mpy(c.x1, c.x0)));

    c.x0 = c.xReadInc();
    c.y1 = c.yReadInc();
    const auto preMac0e = c.a;
    c.a = mac(preMac0e, c.x0, c.y1);
    c.y0 = limit24(preMac0e);
    c.x1 = c.xReadInc();
    c.a = sub(c.a, mpy(c.y0, c.x0));
    c.y0 = c.yReadDec();
    const auto preMac11 = c.a;
    c.a = mac(preMac11, c.y1, c.x1);
    c.yWriteInc(limit24(preMac11));

    const auto preMac13 = c.a;
    c.a = mac(preMac13, c.y0, c.x0);
    c.y1 = limit24(preMac13);
    c.a = sub(c.a, mpy(c.x0, c.y1));
    const auto oldX1 = c.x1;
    const auto preMac16 = c.a;
    c.a = mac(preMac16, oldX1, c.y0);
    c.x1 = c.xReadInc();
    c.yWriteInc(limit24(preMac16));

    c.y1 = mask24(memory.readX(inputAddress));
    const auto preMac18 = c.a;
    c.a = mac(preMac18, c.x1, c.x0);
    c.y0 = limit24(preMac18);
    // 19: MAC sees the old X1/Y0; the parallel load changes X1 only
    // after its source values have been consumed.
    c.a = sub(c.a, mpy(c.y0, c.x0));
    c.x1 = c.xReadDec();
    c.b = from24(c.y0);
    const auto preMac1b = c.a;
    c.a = mac(preMac1b, c.x1, c.x0);
    c.xWriteInc(limit24(preMac1b));
    c.y0 = limit24(preMac1b);
    c.a = sub(c.a, mpy(c.y0, c.x0));
    c.b = from24(c.y0);
    c.b = c.a;
    c.xWriteInc(limit24(c.a));
    memory.writeY(c.r4 - 4, limit24(c.a));

    // The template's final instruction is `move b,x:$0000`.  The generated
    // 101 relocation uses the graph input address for this absolute output
    // slot, so retain the word in memory as well as returning it to the graph
    // scheduler.  Omitting this write makes an isolated state comparison fail
    // even when the returned sample is correct.
    if (!compiled) memory.writeX(inputAddress, limit24(c.b));

    return limit24(c.b);
}

Word24 runFilterF92(ModuleCursor& c, const std::size_t inputAddress) noexcept
{
    return filterF92(c, inputAddress, false);
}

Word24 runCompiledFilterF92(ModuleCursor& c) noexcept
{
    // P:2cd already prefetched X0/Y0. P:2ef stores the result into X:12
    // after reading its previous value; X:10 retains the oscillator sample.
    return filterF92(c, 0x10, true);
}

StereoWords runOutput4(ModuleCursor& c) noexcept
{
    auto& memory = *c.memory;
    c.x0 = memory.readX(0x12);
    memory.writeX(0x12, limit24(c.b));
    c.y0 = memory.readX(0x13);
    c.a = mpy(c.y0, c.x0);
    c.a = asl(c.a, 2);
    c.r1 = memory.readX(0x04);
    memory.writeX(0x14, limit24(c.a));

    c.n1 = sign24(c.yReadInc());
    if(c.n1 < 0 && static_cast<std::size_t>(-c.n1) > c.r1)
    {
        c.memory->fault = true;
        return {};
    }
    const auto firstR1 = c.r1;
    c.a = from24(memory.readY(firstR1));
    c.r1 = static_cast<std::size_t>(static_cast<int64_t>(firstR1) + c.n1);
    c.a = from24(memory.readY(c.r1));
    ++c.r1;
    c.y0 = memory.readX(0x14);
    c.y1 = c.y0;
    c.x0 = c.xReadInc();
    c.a = mac(c.a, c.y0, c.x0);
    c.b = from24(memory.readY(c.r1));
    --c.r1;
    c.b = mac(c.b, c.x0, c.y1);
    memory.writeY(c.r1++, limit24(c.a));
    memory.writeY(c.r1++, limit24(c.b));
    return {limit24(c.a), limit24(c.b)};
}
}

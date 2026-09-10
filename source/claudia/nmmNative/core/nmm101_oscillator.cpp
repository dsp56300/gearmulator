#include "nmm101_kernels.h"

namespace nmm::native
{
namespace
{
bool lessThan(const ModuleCursor& c) noexcept { return c.ccrN != c.ccrV; }
bool greaterThan(const ModuleCursor& c) noexcept
{
    return !c.ccrZ && !lessThan(c);
}
bool greaterEqual(const ModuleCursor& c) noexcept { return !lessThan(c); }
bool lessEqual(const ModuleCursor& c) noexcept { return c.ccrZ || lessThan(c); }

struct CcrState
{
    bool c = false, v = false, n = false, z = false;
};

CcrState saveCcr(const ModuleCursor& c) noexcept
{
    return {c.ccrC, c.ccrV, c.ccrN, c.ccrZ};
}

void restoreCcr(ModuleCursor& c, CcrState state) noexcept
{
    c.ccrC = state.c;
    c.ccrV = state.v;
    c.ccrN = state.n;
    c.ccrZ = state.z;
}

void flagsFromResult(ModuleCursor& c, Acc56 value, bool overflow = false) noexcept
{
    const auto v = wrap56(value);
    c.ccrN = v < 0;
    c.ccrZ = v == 0;
    c.ccrV = overflow;
}

void test(ModuleCursor& c, Acc56 value) noexcept
{
    const auto oldCarry = c.ccrC;
    flagsFromResult(c, value);
    c.ccrC = oldCarry;
}

void compare(ModuleCursor& c, Acc56 left, Acc56 right, bool magnitude = false) noexcept
{
    const auto l = magnitude ? abs(left) : left;
    const auto r = magnitude ? abs(right) : right;
    const auto result = sub(l, r);
    // CMP restores its destination accumulator.  Its V bit is explicitly
    // cleared by the DSP, while C is the unsigned borrow.
    c.ccrN = result < 0;
    c.ccrZ = result == 0;
    c.ccrV = false;
    c.ccrC = static_cast<uint64_t>(wrap56(r)) > static_cast<uint64_t>(wrap56(l));
}

void addA(ModuleCursor& c, Acc56 value) noexcept
{
    c.a = add(c.a, value);
    flagsFromResult(c, c.a);
}

void addB(ModuleCursor& c, Acc56 value) noexcept
{
    c.b = add(c.b, value);
    flagsFromResult(c, c.b);
}

void subB(ModuleCursor& c, Acc56 value) noexcept
{
    c.b = sub(c.b, value);
    flagsFromResult(c, c.b);
}

void negateB(ModuleCursor& c) noexcept
{
    c.b = neg(c.b);
    // NEG does not write V on the 56300; N/Z are updated and V is retained.
    c.ccrN = c.b < 0;
    c.ccrZ = c.b == 0;
}

void shiftLeftB(ModuleCursor& c, unsigned count) noexcept
{
    const auto old = c.b;
    c.b = asl(c.b, count);
    // DSP V is set when the sign bit changes during the shift.  The native
    // graph never enables saturation mode, so this bit test is sufficient
    // and remains defined for the full 56-bit canonical accumulator.
    bool overflow = false;
    if(count != 0 && count < 56)
    {
        const auto dropped = static_cast<uint64_t>(wrap56(old))
            >> (56 - count);
        const auto sign = (dropped >> (count - 1)) & 1u;
        overflow = dropped != (sign ? ((uint64_t(1) << count) - 1u) : 0u);
    }
    flagsFromResult(c, c.b, overflow);
    c.ccrC = count != 0 && count < 56
        ? ((static_cast<uint64_t>(wrap56(old)) >> (56 - count)) & 1u) != 0
        : false;
}

void divStep(ModuleCursor& c) noexcept
{
    // This is the DSP56300 non-restoring DIV step, copied from
    // dsp_ops_alu.inl with the emulator's left-aligned register shifted back
    // into the native right-aligned representation.  Carry enters bit 0;
    // the source word is sign-extended at A1 (bits 47..24).
    const auto old = c.b;
    const bool oldMsb = (static_cast<uint64_t>(wrap56(old)) & (uint64_t(1) << 55)) != 0;
    const bool sourceMsb = (mask24(c.x0) & 0x800000u) != 0;
    const bool addSource = oldMsb != sourceMsb;
    c.b = wrap56Raw((static_cast<uint64_t>(wrap56(old)) << 1) | (c.ccrC ? 1u : 0u));
    c.b = addSource ? add(c.b, from24(c.x0)) : sub(c.b, from24(c.x0));
    c.ccrC = c.b >= 0; // C is set when result bit 55 is clear.
    c.ccrN = c.b < 0;
    c.ccrZ = c.b == 0;
    // DIV leaves V as the sign change caused by its internal shift.
    c.ccrV = oldMsb != ((static_cast<uint64_t>(wrap56(c.b)) & (uint64_t(1) << 55)) != 0);
}

void divSeven(ModuleCursor& c) noexcept
{
    for(unsigned i = 0; i < 7; ++i)
        divStep(c);
}

void clearCarry(ModuleCursor& c) noexcept { c.ccrC = false; }

Word24 readYFull(ModuleCursor& c, std::size_t address) noexcept
{
    return c.memory->readY(address);
}

}

Word24 finishOscillatorType7(ModuleCursor& c) noexcept
{
    // P:$02cc: asr #$02,b,b
    c.b = asr(c.b, 2);
    // P:$02cd: move x:(r3)+,x0 y:(r4)+,y0
    c.x0 = c.xReadInc();
    c.y0 = c.yReadInc();
    // P:$02ce: move b,x:$0010 (full accumulator transfer is limited).
    c.memory->writeX(0x10, limit24(c.b));
    return limit24(c.b);
}

Word24 runOscillatorType7(ModuleCursor& c) noexcept
{
    // P:$0281: MPYR +X0,Y1,A || B,X0.  The parallel move sees old B.
    const auto oldB = c.b;
    c.a = mpyr(c.x0, c.y1);
    c.x0 = limit24(oldB);
    flagsFromResult(c, c.a);

    // P:$0282: TST B || X:(R3)+,B || Y:(R4),Y1.
    test(c, c.b);
    c.b = from24(c.xReadInc());
    c.y1 = c.memory->readY(c.r4);

    // P:$0283..0285: conditional negate/compare and TLT.
    if(lessThan(c))
    {
        const auto condition = saveCcr(c);
        negateB(c);
        restoreCcr(c, condition);
    }
    if(lessThan(c))
        compare(c, c.a, from24(c.y1));
    if(lessThan(c))
        c.b = c.a;

    // P:$0286: TFR X0,A || A,Y1.  The accumulator-to-data transfer is a
    // limited bus transfer; the transfer to A is a signed A1 placement.
    const auto oldA = c.a;
    c.a = from24(c.x0);
    c.y1 = limit24(oldA);

    // P:$0287: TST A || Y1,A; P:$0288 loads the next table word.
    test(c, c.a);
    c.a = from24(c.y1);
    c.y1 = readYFull(c, c.r4);

    // P:$0289..028a: conditional compare followed by TGT A,B.
    if(greaterThan(c))
        compare(c, c.a, from24(c.y1));
    if(greaterThan(c))
        c.b = c.a;

    // P:$028b: MPY -X1,Y0,A || A,X1.
    const auto oldA2 = c.a;
    c.a = neg(mpy(c.x1, c.y0));
    c.x1 = limit24(oldA2);
    flagsFromResult(c, c.a);

    // P:$028c: TFR Y0,A || X:(R3)+,Y0.
    const auto oldY0 = c.y0;
    c.a = from24(oldY0);
    c.y0 = c.xReadInc();

    // P:$028d is a conditional TST.  P:$028e's load is unconditional.
    if(greaterEqual(c))
        test(c, c.a);
    c.a = from24(readYFull(c, c.r4));

    // P:$028f..0290: conditional ADD and TGT.
    if(lessEqual(c))
    {
        const auto condition = saveCcr(c);
        addA(c, from24(c.x0));
        restoreCcr(c, condition);
    }
    if(greaterThan(c))
        c.a = from24(c.y0);

    // P:$0291: SUB A,B || X:(R3)+,Y1.  The source register move observes old
    // R3 but is independent of the ALU operation.
    c.b = sub(c.b, c.a);
    flagsFromResult(c, c.b);
    c.y1 = c.xReadInc();

    // P:$0292: CMPM X0,B || A1,A; P:$0293 chooses the division path.
    compare(c, c.b, from24(c.x0), true);
    c.a = from24(to24(c.a));
    if(greaterThan(c))
    {
        // P:$02ab alternate side of the selected branch.
        subB(c, from24(c.x0));
        c.r3++;
        compare(c, c.b, from24(c.x0), true);
        if(lessThan(c))
        {
            // P:$02b4..02c6.
            c.b = abs(c.b);
            test(c, c.b);
            clearCarry(c);
            divSeven(c);
            shiftLeftB(c, 0x29);
            compare(c, c.a, from24(c.x1));
            if(lessThan(c))
            {
                const auto condition = saveCcr(c);
                negateB(c);
                restoreCcr(c, condition);
            }
            addA(c, from24(c.x0));
            compare(c, c.a, from24(c.x1));
            const auto oldB2 = c.b;
            c.y0 = limit24(oldB2);
            c.b = from24(c.y1);
            if(lessThan(c))
            {
                const auto condition = saveCcr(c);
                negateB(c);
                restoreCcr(c, condition);
            }
            c.memory->writeY(c.r4++, to24(c.a));
            addB(c, from24(c.y0));
            c.a = from24(c.x1);
            c.b = addr(c.b, c.a);
            c.a = from24(readYFull(c, c.r4));
        }
        else
        {
            // P:$02ae..02b3.
            addA(c, from24(c.x0));
            c.b = from24(c.y1);
            compare(c, c.a, from24(c.x1));
            if(lessThan(c))
            {
                const auto condition = saveCcr(c);
                negateB(c);
                restoreCcr(c, condition);
            }
            const auto oldA3 = c.a;
            c.a = from24(c.x1);
            c.memory->writeY(c.r4++, to24(oldA3));
            // P:$02b2's source is the full accumulator A, not a 24-bit bus
            // transfer.  Its A1-only sibling is the preceding memory write.
            addB(c, c.a);
            c.a = from24(readYFull(c, c.r4));
        }
    }
    else
    {
        // P:$0294..02aa.  Address update and C clear are independent of the
        // ALU instructions in this packet.
        c.b = abs(c.b);
        c.r3++;
        clearCarry(c);
        divSeven(c);
        shiftLeftB(c, 0x29);
        compare(c, c.a, from24(c.x1));
        if(lessThan(c))
        {
            const auto condition = saveCcr(c);
            negateB(c);
            restoreCcr(c, condition);
        }
        addA(c, from24(c.x0));
        c.a = from24(to24(c.a));
        compare(c, c.a, from24(c.x1));
        const auto oldB3 = c.b;
        c.y0 = limit24(oldB3);
        if(lessThan(c))
        {
            const auto condition = saveCcr(c);
            subB(c, from24(c.y1));
            restoreCcr(c, condition);
        }
        if(greaterEqual(c))
        {
            const auto condition = saveCcr(c);
            addB(c, from24(c.y1));
            restoreCcr(c, condition);
        }
        c.memory->writeY(c.r4++, to24(c.a));
        // P:$02a7: MOVE B,B is an explicit accumulator-to-accumulator
        // limited transfer.  It changes B to the signed A1 placement of
        // LIMIT(B) before the following full accumulator ADD.
        c.b = from24(limit24(c.b));
        addB(c, from24(c.y0));
        c.a = from24(c.x1);
        c.b = addr(c.b, c.a);
        c.a = from24(readYFull(c, c.r4));
    }

    // P:$02c7..02cc common tail.
    c.b = asl(c.b, 1);
    flagsFromResult(c, c.b);
    c.b = asr(c.b, 2);
    flagsFromResult(c, c.b);
    c.a = add(c.a, c.b);
    flagsFromResult(c, c.a);
    c.r3++;
    c.memory->writeY(c.r4++, limit24(c.b));
    c.b = c.a;
    flagsFromResult(c, c.b);
    return finishOscillatorType7(c);
}
}

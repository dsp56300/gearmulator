#pragma once

#include "dsp56300_fixed.h"

#include <cstddef>

namespace nmm::native
{
struct Graph101Layout
{
    static constexpr std::size_t controlEntry = 0x175;
    static constexpr std::size_t envelopeOutput = 0x13;
    static constexpr std::size_t sampleEntry = 0x1ba;
    static constexpr std::size_t oscillatorEntry = 0x1df;
    static constexpr std::size_t filterEntry = 0x2cf;
    static constexpr std::size_t amplifierEntry = 0x2ee;
    static constexpr std::size_t outputEntry = 0x2f5;
    static constexpr std::size_t defaultCoefficientInput = 0x10;
};

struct GraphMemory
{
    Word24* x = nullptr;
    Word24* y = nullptr;
    std::size_t xWords = 0;
    std::size_t yWords = 0;
    bool fault = false;

    Word24 readX(std::size_t address) noexcept
    {
        if(address >= xWords) { fault = true; return 0; }
        return mask24(x[address]);
    }
    Word24 readY(std::size_t address) noexcept
    {
        if(address >= yWords) { fault = true; return 0; }
        return mask24(y[address]);
    }
    void writeX(std::size_t address, Word24 value) noexcept
    {
        if(address >= xWords) { fault = true; return; }
        x[address] = mask24(value);
    }
    void writeY(std::size_t address, Word24 value) noexcept
    {
        if(address >= yWords) { fault = true; return; }
        y[address] = mask24(value);
    }
};

struct ModuleCursor
{
    GraphMemory* memory = nullptr;
    std::size_t r3 = 0;
    std::size_t r4 = 0;
    std::size_t r1 = 0;
    std::size_t r2 = 0;
    int32_t n1 = 0;
    std::size_t r5 = 0;
    std::size_t n2 = 0;
    std::size_t n5 = 0;
    Acc56 a = 0;
    Acc56 b = 0;
    Word24 x0 = 0, x1 = 0, y0 = 0, y1 = 0;

    // The selected oscillator uses conditional ALU packets.  Keep the
    // condition bits at the graph boundary instead of silently deriving a
    // host signed comparison from an accumulator whose extension may be
    // non-normalised.
    bool ccrC = false;
    bool ccrV = false;
    bool ccrN = false;
    bool ccrZ = false;

    Word24 xReadInc() noexcept { return memory->readX(r3++); }
    Word24 yReadInc() noexcept { return memory->readY(r4++); }
    Word24 xReadDec() noexcept { return memory->readX(r3--); }
    Word24 yReadDec() noexcept { return memory->readY(r4--); }
    void xWriteInc(Word24 value) noexcept { memory->writeX(r3++, value); }
    void yWriteInc(Word24 value) noexcept { memory->writeY(r4++, value); }
};

// Exact straight-line translation of module 092's sample template as linked
// into 101.  `inputAddress` is X:$0010 in the generated 101 program.  The
// caller supplies r3/r4 at the filter's allocated coefficient/state region.
Word24 runFilterF92(ModuleCursor& cursor, std::size_t inputAddress = 0x10) noexcept;

// Compiled 101 P:2cf..2ed: consumes prefetched X0/Y0 and leaves B for output.
Word24 runCompiledFilterF92(ModuleCursor& cursor) noexcept;

// Exact output-stage multiply and stereo state-store from 101 P:$02ee..$02fe.
// Returns the two 24-bit words written through r1; the caller converts those
// words to the board's serial format.
struct StereoWords { Word24 left = 0, right = 0; };
StereoWords runOutput4(ModuleCursor& cursor) noexcept;

// The selected 101 type-7 path jumps to P:$0281.  Its common tail is
// independent of the waveform branch and is kept separate while the branch's
// table/DIV path is being differential-tested.
Word24 finishOscillatorType7(ModuleCursor& cursor) noexcept;

// Execute the reachable P:$0281..$02ce type-7 body in the generated 101
// graph.  The caller positions the cursor/registers at the P:$01fa JMP
// boundary; all table/state accesses are through GraphMemory.
Word24 runOscillatorType7(ModuleCursor& cursor) noexcept;

// P:1df..1f9, using the actual 101 oscillator parameter words and shared LUTs.
void runOscillatorType7Setup(ModuleCursor& cursor) noexcept;

// The first implementation milestone intentionally exposes the module
// boundaries separately.  This lets the hardware harness compare each
// module's X/Y state against the emulator before the complete generated graph
// is linked.  These functions do not use floating point or host lookup tables.
}

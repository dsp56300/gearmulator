#pragma once

// Small, freestanding DSP56300 arithmetic subset used by the generated 101
// graph.  Values in X/Y are unsigned 24-bit words containing signed Q1.23
// fractions.  Accumulator values are signed 56-bit values in the DSP's
// right-aligned mathematical representation.  The real register file stores
// the same value left aligned by eight bits; keeping the canonical value here
// makes the shifts and the 24-bit transfers explicit and deterministic on
// both desktop and Cortex-M7.

#include <cstdint>

namespace nmm::native
{
using Word24 = uint32_t;
using SWord24 = int32_t;
using Acc56 = int64_t;

constexpr Word24 kMask24 = 0x00ffffffu;
constexpr Acc56 kAccMax = 0x007fffffffffffffll;
constexpr Acc56 kAccMin = -0x0080000000000000ll;

constexpr Word24 mask24(Word24 value) noexcept { return value & kMask24; }

constexpr SWord24 sign24(Word24 value) noexcept
{
    value &= kMask24;
    return static_cast<SWord24>(value) - ((value & 0x00800000u) ? 0x01000000 : 0);
}

constexpr Acc56 wrap56Raw(uint64_t raw) noexcept
{
    // DSP accumulators retain 56 bits.  The cast is well-defined for the
    // expected product/sum range and the explicit sign extension avoids
    // depending on implementation-defined signed shifts.
    raw &= 0x00ffffffffffffffull;
    return static_cast<Acc56>(raw)
        - ((raw & 0x0080000000000000ull) ? static_cast<Acc56>(0x0100000000000000ull) : 0);
}

constexpr Acc56 wrap56(Acc56 value) noexcept
{
    return wrap56Raw(static_cast<uint64_t>(value));
}

constexpr Acc56 from24(Word24 value) noexcept
{
    // A MOVE Xn,A places a 24-bit word in A1 (bits 47..24).  MPY/MAC
    // operands remain unshifted X/Y words; this helper is for accumulator
    // loads and therefore carries the A1 placement explicitly.
    return static_cast<Acc56>(sign24(value)) * static_cast<Acc56>(1ll << 24);
}

constexpr Word24 to24(Acc56 value) noexcept
{
    // All Acc56 values produced by this header are already canonical
    // 56-bit values.  A bus transfer takes A1 (bits 47..24), so no second
    // modulo operation is needed here.
    return static_cast<Word24>((static_cast<uint64_t>(value) >> 24) & kMask24);
}

// Fractional MPY/MAC: 24 x 24 followed by the DSP's one-bit fractional
// product shift.  No rounding or saturation is implicit in MPY/MAC.
constexpr Acc56 mpy(Word24 left, Word24 right) noexcept
{
    const auto product = static_cast<int64_t>(sign24(left)) * static_cast<int64_t>(sign24(right));
    // The signed 24-bit product doubled is bounded by 2^48 in magnitude,
    // well inside the signed 56-bit accumulator.  MPY therefore cannot
    // overflow the accumulator and does not need wrap56Raw here.
    return product * 2;
}

constexpr Acc56 mac(Acc56 accumulator, Word24 left, Word24 right) noexcept
{
    return wrap56Raw(static_cast<uint64_t>(accumulator) + static_cast<uint64_t>(mpy(left, right)));
}

constexpr Acc56 neg(Acc56 value) noexcept
{
    return wrap56Raw(0u - static_cast<uint64_t>(value));
}

constexpr Acc56 add(Acc56 left, Acc56 right) noexcept
{
    return wrap56Raw(static_cast<uint64_t>(left) + static_cast<uint64_t>(right));
}

constexpr Acc56 sub(Acc56 left, Acc56 right) noexcept
{
    return wrap56Raw(static_cast<uint64_t>(left) - static_cast<uint64_t>(right));
}

constexpr Acc56 asl(Acc56 value, unsigned count) noexcept
{
    return count >= 56 ? 0 : wrap56Raw(static_cast<uint64_t>(value) << count);
}

constexpr Acc56 asr(Acc56 value, unsigned count) noexcept
{
    if(count >= 56)
        return value < 0 ? -1 : 0;
    const auto divisor = static_cast<Acc56>(1) << count;
    if(value >= 0)
        return value / divisor;
    // Arithmetic right shift is floor division, whereas C++ signed division
    // truncates toward zero.
    return -(((-value) + divisor - 1) / divisor);
}

constexpr Acc56 addl(Acc56 value, Acc56 other) noexcept
{
    return add(asl(value, 1), other);
}

constexpr Acc56 addr(Acc56 value, Acc56 other) noexcept
{
    return add(asr(value, 1), other);
}

constexpr Acc56 abs(Acc56 value) noexcept
{
    return value < 0 ? neg(value) : value;
}

// MPYR/RND in the default 24-bit mode.  The 56300 uses convergent (ties to
// even) rounding unless RM is set.  The generated 101 code runs in the
// default mode, so this helper intentionally has no hidden host float path.
constexpr Acc56 round24(Acc56 value) noexcept
{
    constexpr Acc56 roundBit = static_cast<Acc56>(1) << 23;
    constexpr Acc56 lowerMask = (static_cast<Acc56>(1) << 24) - 1;
    const auto added = add(value, roundBit);
    const auto raw = static_cast<uint64_t>(added);
    if((static_cast<uint64_t>(value) & static_cast<uint64_t>(lowerMask))
       == static_cast<uint64_t>(roundBit))
        return wrap56Raw(raw & ~static_cast<uint64_t>(lowerMask)
                         & ~(static_cast<uint64_t>(1) << 24));
    return wrap56Raw(raw & ~static_cast<uint64_t>(lowerMask));
}

constexpr Acc56 mpyr(Word24 left, Word24 right) noexcept
{
    return round24(mpy(left, right));
}

constexpr Word24 limit24(Acc56 value) noexcept
{
    // LIMIT is only needed at actual bus transfers.  Acc56 is a canonical
    // 56-bit value by contract for every arithmetic helper in this header,
    // so re-masking it here would only repeat the modulo operation.  This is
    // the DSP's Q1.23 endpoint behavior, including the asymmetric positive
    // endpoint.
    const auto v = value;
    constexpr auto busMin = -static_cast<Acc56>(0x800000ll) * static_cast<Acc56>(1ll << 24);
    if(v < busMin)
        return 0x800000u;
    if(v > (static_cast<Acc56>(0x7fffff) << 24))
        return 0x7fffffu;
    return to24(v);
}
}

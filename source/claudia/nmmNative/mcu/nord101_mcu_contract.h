#pragma once

// Evidence-backed MCU-side transport for the fixed 101.pch fixture.
//
// This header deliberately stops at the boundaries that are recovered from
// the OS 3.03b image.  In particular, it does not guess the editor-to-DSP
// parameter conversion or the velocity curve.  Those require additional
// native-call traces before they can be put on the Teensy path.

#include <cstdint>

namespace nmm::native::mcu
{
constexpr std::uint32_t kMask24 = 0x00ffffffu;
constexpr std::uint32_t kFirmwareBase = 0x100000u;

constexpr std::uint8_t kOscA1Type = 7;
constexpr std::uint8_t kFilterF1Type = 92;
constexpr std::uint8_t kAdsrEnv1Type = 20;
constexpr std::uint8_t kTwoOutputsType = 4;

// 101.pch module parameters, in patch-file order.  The first value in each
// row is the module's parameter count; the remaining values are editor bytes.
constexpr std::uint8_t kOscA1ParameterCount = 10;
constexpr std::uint8_t kOscA1Parameters[kOscA1ParameterCount] =
    {64, 64, 64, 26, 3, 0, 0, 0, 0, 0};
constexpr std::uint8_t kFilterF1ParameterCount = 7;
constexpr std::uint8_t kFilterF1Parameters[kFilterF1ParameterCount] =
    {65, 64, 77, 127, 0, 2, 0};
constexpr std::uint8_t kAdsrEnv1ParameterCount = 6;
constexpr std::uint8_t kAdsrEnv1Parameters[kAdsrEnv1ParameterCount] =
    {1, 0, 0, 127, 67, 0};
constexpr std::uint8_t kTwoOutputsParameterCount = 3;
constexpr std::uint8_t kTwoOutputsParameters[kTwoOutputsParameterCount] =
    {115, 0, 0};

constexpr std::uint32_t mask24(std::uint32_t value) noexcept { return value & kMask24; }

// The 68k constructs ((note << 23) - 0x20000000), then the DSP host write
// carries that value down by eight bits.  This is the exact 24-bit transport
// sequence observed for the 101 snapshots (60 -> fe0000, 72 -> 040000).
constexpr std::uint32_t pitchWord(std::uint8_t midiNote) noexcept
{
    return mask24((static_cast<std::uint32_t>(midiNote) << 15) - 0x00200000u);
}

constexpr std::uint32_t gateWord(bool held) noexcept { return held ? 0x00200000u : 0u; }

// The 128-entry table at 0x14acb2 is the DSP velocity conversion used by the
// 101 note path. It is exactly round(velocity * 0x200000 / 127), expressed
// with integer half-up rounding. Values are written to X:0x0d.
constexpr std::uint32_t velocityWordForMidi(std::uint8_t midiVelocity) noexcept
{
    return (static_cast<std::uint32_t>(midiVelocity) * 0x00200000u + 63u) / 127u;
}

struct VoiceTransport
{
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    std::uint32_t velocityWord = 0;
    bool held = false;
    std::uint32_t pitch = 0;
    std::uint32_t gate = 0;

    // Returns false for MIDI note-on velocity zero (the firmware dispatches it
    // through note-off) and for notes outside the 7-bit MIDI range.
    constexpr bool noteOn(std::uint16_t midiNote, std::uint8_t midiVelocity) noexcept
    {
        if(midiNote > 127 || midiVelocity == 0)
        {
            velocity = 0;
            velocityWord = 0;
            noteOff();
            return false;
        }
        note = static_cast<std::uint8_t>(midiNote);
        velocity = midiVelocity;
        velocityWord = velocityWordForMidi(midiVelocity);
        held = true;
        pitch = pitchWord(note);
        gate = gateWord(true);
        return true;
    }

    constexpr void noteOff() noexcept
    {
        held = false;
        gate = gateWord(false);
    }
};

constexpr std::uint32_t kObservedVelocity100Word = 0x00193265u;

// The MCU uses a signed 32-bit long for the current and target coefficient.
// Keep subtraction and arithmetic right shift explicit so host and Cortex-M7
// builds have the same wrap and negative-rounding behaviour as the 68k.
constexpr std::int64_t signed32(std::uint32_t bits) noexcept
{
    return (bits & 0x80000000u) ? static_cast<std::int64_t>(bits) - 0x100000000ll
                                : static_cast<std::int64_t>(bits);
}

constexpr std::uint32_t bits32(std::int64_t value) noexcept
{
    return static_cast<std::uint32_t>(value) & 0xffffffffu;
}

constexpr std::int64_t sub32(std::uint32_t left, std::uint32_t right) noexcept
{
    return signed32(bits32(static_cast<std::int64_t>(left) - static_cast<std::int64_t>(right)));
}

constexpr std::int64_t arithmeticShiftRight(std::int64_t value, std::uint8_t count) noexcept
{
    if(count == 0)
        return value;
    if(count >= 63)
        return value < 0 ? -1 : 0;
    const auto divisor = static_cast<std::int64_t>(1) << count;
    if(value >= 0)
        return value / divisor;
    return -(((-value) + divisor - 1) / divisor);
}

struct SmoothingRecord
{
    std::uint32_t current = 0;
    std::uint32_t target = 0;
    std::uint8_t shift = 6;
    bool active = false;

    // One execution of the proven 10c958..10c99a record body.  Returns true
    // when the record snapped to target and became inactive.
    constexpr bool step() noexcept
    {
        if(!active)
            return false;

        const auto delta = arithmeticShiftRight(sub32(current, target), shift);
        current = bits32(static_cast<std::int64_t>(signed32(current)) - delta);
        const auto difference = sub32(current, target);
        if(difference > -255 && difference < 255)
        {
            current = target;
            active = false;
            return true;
        }
        return false;
    }
};

struct ModuleBankContract
{
    std::uint8_t type;
    std::uint16_t sampleX;
    std::uint16_t sampleY;
    std::uint16_t sampleP;
    std::uint16_t controlX;
    std::uint16_t controlY;
    std::uint16_t controlP;
};

constexpr ModuleBankContract k101Banks[] = {
    {kOscA1Type, 12, 5, 240, 0, 0, 0},
    {kFilterF1Type, 6, 4, 34, 4, 6, 39},
    {kAdsrEnv1Type, 0, 0, 6, 15, 3, 32},
    {kTwoOutputsType, 1, 1, 12, 0, 0, 0},
};

// Fixed 101 binding/state addresses.  These are useful for a reference
// harness; native code must not treat them as generic patch allocation rules.
constexpr std::uint16_t kPitchX = 0x000f;
constexpr std::uint16_t kVelocityX = 0x000d;
constexpr std::uint16_t kGateX = 0x000e;
constexpr std::uint16_t kEnvelopeX = 0x0013;
constexpr std::uint16_t kAmplifierX = 0x0014;
constexpr std::uint16_t kMasterCoefficientX = 0x005f;
constexpr std::uint32_t kObservedHeldEnvelope = 0x001fffffu;

struct CoefficientWord
{
    std::uint16_t address;
    std::uint32_t value;
};

// Ready-state words captured from the firmware-generated poly binding
// (00100060006001e00073006a0175) before the note-on.  Allocation is from the
// module bank lengths above: OscA1 sample X/Y, FilterF1 sample X/Y, the output
// sample state, then FilterF1/ADSR-Env1 control X/Y.  These are the settled
// coefficients/state for 101's fixed editor values, not a general conversion
// curve for arbitrary patches.
constexpr CoefficientWord k101PolySampleX[] = {
    {0x60, 0x400000}, {0x61, 0x000000}, {0x62, 0x01f00a}, {0x63, 0xf68000},
    {0x64, 0x01c20c}, {0x65, 0x000000}, {0x66, 0x7d70a4}, {0x67, 0x7fffff},
    {0x68, 0x7fffff}, {0x69, 0x7fffff}, {0x6a, 0xadd4d5}, {0x6b, 0x092aa9},
    {0x6c, 0x293a6a}, {0x6d, 0x10aaaa}, {0x6e, 0x7c5dce}, {0x6f, 0x4ccccd},
    {0x70, 0xfea637}, {0x71, 0x08b788}, {0x72, 0x2d3094},
};
constexpr CoefficientWord k101PolySampleY[] = {
    {0x60, 0x000000}, {0x61, 0x000000}, {0x62, 0x000000}, {0x63, 0x724238},
    {0x64, 0x1ac28f}, {0x65, 0x08b788}, {0x66, 0x320000}, {0x67, 0xf8930a},
    {0x68, 0xf791f7}, {0x69, 0x000000},
};
constexpr CoefficientWord k101PolyControlX[] = {
    {0x73, 0x400000}, {0x74, 0x01f009}, {0x75, 0x03afa7}, {0x76, 0x2aaaab},
    {0x77, 0x00007b}, {0x78, 0x000000}, {0x79, 0x000000}, {0x7a, 0x000084},
    {0x7b, 0x055555}, {0x7c, 0x000000}, {0x7d, 0x7fffff}, {0x7e, 0x3fffff},
    {0x7f, 0x573494}, {0x80, 0x000000}, {0x81, 0x000000},
};
constexpr CoefficientWord k101PolyControlY[] = {
    {0x6a, 0x7fffff}, {0x6b, 0x000000}, {0x6c, 0x7fffff}, {0x6d, 0x00006e},
    {0x6e, 0x400000}, {0x6f, 0x155555}, {0x70, 0x000081}, {0x71, 0x000081},
    {0x72, 0x000000},
};

// 101's output parameter is 115.  The table at 0x14c3b2 is the firmware
// conversion used by the native master path; this one fixed lookup is safe to
// carry into the reference Teensy harness without pretending to support the
// unresolved module parameter callback family.
constexpr std::uint32_t k101OutputParameter = 115;
constexpr std::uint32_t k101OutputCoefficient = 0x00016605u;

} // namespace nmm::native::mcu

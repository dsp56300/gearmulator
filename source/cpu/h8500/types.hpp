// Basic types shared by the H8/500 emulator core.
#pragma once
#include "common/types.hpp"

namespace h8500 {

using emu::u8;
using emu::u16;
using emu::u32;
using emu::u64;
using emu::s8;
using emu::s16;
using emu::s32;
using emu::s64;

// Operand size.  H8/500 has byte and word operands only.
enum class Size : u8 { None = 0, Byte = 1, Word = 2 };

inline constexpr unsigned size_bytes(Size s) { return s == Size::Word ? 2u : (s == Size::Byte ? 1u : 0u); }
inline constexpr u32 size_mask(Size s) { return s == Size::Word ? 0xFFFFu : 0xFFu; }
inline constexpr u32 size_msb(Size s) { return s == Size::Word ? 0x8000u : 0x80u; }
inline constexpr unsigned size_bits(Size s) { return s == Size::Word ? 16u : 8u; }

// Bus access class of an address range, used for cycle accounting.
// The manuals express all instruction timings assuming 16-bit-wide,
// two-state accesses (on-chip memory / 16-bit 2-state external area) and
// give correction terms for every other combination of bus width and
// access-state count.  See timing.hpp.
enum class BusClass : u8 {
  W16_S2 = 0,  // 16-bit bus, 2-state access (on-chip RAM/ROM, H8/510 2-state external area)
  W16_S3,      // 16-bit bus, 3-state access (H8/510/570 3-state external area)
  W8_S2,       // 8-bit bus, 2-state access
  W8_S3,       // 8-bit bus, 3-state access (on-chip register field, H8/532 external area)
};

inline constexpr bool bus_is_16bit(BusClass c) { return c == BusClass::W16_S2 || c == BusClass::W16_S3; }
inline constexpr bool bus_is_3state(BusClass c) { return c == BusClass::W16_S3 || c == BusClass::W8_S3; }

}  // namespace h8500

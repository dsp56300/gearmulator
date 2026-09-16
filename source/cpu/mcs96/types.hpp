// Basic types shared by the MCS-96 emulator core.
#pragma once
#include "common/types.hpp"

namespace mcs96 {

using emu::u8;
using emu::u16;
using emu::u32;
using emu::u64;
using emu::s8;
using emu::s16;
using emu::s32;
using emu::s64;

// The two family members: the NMOS 8x9x (8095/8096/8098, three oscillator
// periods per state) and the CHMOS 80C196KB (two per state, SFR windows,
// extra instructions and interrupt sources).
enum class Variant : u8 { I8x9x, I80C196KB };

// PSW layout: the low byte is INT_MASK, the high byte the flags.
enum PswBit : u16 {
  kSt = 1u << 8,   // sticky bit (shifted-out ones)
  kI = 1u << 9,    // global interrupt enable
  kX = 1u << 10,   // unused, always 0
  kC = 1u << 11,   // carry
  kVt = 1u << 12,  // overflow trap (sticky V)
  kV = 1u << 13,   // overflow
  kN = 1u << 14,   // negative
  kZ = 1u << 15,   // zero
  kIntMaskBits = 0x00FF,
  kFlagBits = 0xFF00,
};

constexpr u16 kResetPc = 0x2080;
constexpr u8 kSpRegister = 0x18;
constexpr u16 kSoftwareTrapVector = 0x2010;
constexpr u16 kUnimplementedOpcodeVector = 0x2012;

}  // namespace mcs96

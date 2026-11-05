// Motorola MC6801 / Hitachi HD6301 / Motorola MC68HC11 family core: shared types.
//
// Three members are built: the MC6803 (an MC6801 without ROM, modes 2 and 3),
// the HD6303R (an HD6301V1 with its ROM disabled, modes 1, 2 and 4) and the
// MC68HC11A0 (an HC11A8 without ROM and EEPROM, expanded multiplexed mode).
// The first two carry the same on-chip block in the register field at
// $0000-$001F (ports 1-4, the 16-bit timer, the SCI, the RAM control
// register) and 128 bytes of RAM at $0080. The HD6301 core is Hitachi's CMOS
// redesign: the MC6801 instruction set plus AIM/OIM/EIM/TIM, XGDX and SLP,
// pipelined instruction timing, and a TRAP on undefined opcodes and on
// fetches from the register area. The HC11 keeps the MC6801's bus timing
// and adds the Y register (prebyte pages $18, $1A and $CD), the bit
// instructions BSET/BCLR/BRSET/BRCLR, IDIV/FDIV, XGDX/XGDY and STOP, the X
// interrupt mask for XIRQ and an illegal-opcode trap; its peripheral block
// is a different one (hc11periph.hpp).
#pragma once
#include "common/types.hpp"

namespace m6801 {

using emu::s16;
using emu::s32;
using emu::s64;
using emu::s8;
using emu::u16;
using emu::u32;
using emu::u64;
using emu::u8;

enum class Family : u8 { M6801, HD6301, HC11 };
enum class ChipModel : u8 { MC6803, HD6303R, MC68HC11A0 };

constexpr Family family_of(ChipModel m) {
  return m == ChipModel::HD6303R ? Family::HD6301 : m == ChipModel::MC68HC11A0 ? Family::HC11 : Family::M6801;
}
// Operating modes the part supports (latched from P20-P22 at reset; on the
// HC11 from MODA / MODB, of which only the expanded multiplexed mode, 1, is
// built).
constexpr bool mode_supported(ChipModel m, int mode) {
  if (m == ChipModel::MC68HC11A0) return mode == 1;
  return m == ChipModel::MC6803 ? (mode == 2 || mode == 3) : (mode == 1 || mode == 2 || mode == 4);
}

// Condition code register. On the MC6801 / HD6301 bits 7 and 6 do not exist
// and read as ones; on the HC11 they are S (STOP disable) and X (XIRQ mask),
// both set by reset, and X can be cleared by software but not set again.
constexpr u8 kC = 0x01, kV = 0x02, kZ = 0x04, kN = 0x08, kI = 0x10, kH = 0x20, kCcrFixed = 0xC0;
constexpr u8 kX = 0x40, kS = 0x80;

// Vector addresses, highest priority first.
constexpr u16 kVecReset = 0xFFFE, kVecTrap = 0xFFEE, kVecNmi = 0xFFFC, kVecSwi = 0xFFFA, kVecIrq1 = 0xFFF8,
              kVecIcf = 0xFFF6, kVecOcf = 0xFFF4, kVecTof = 0xFFF2, kVecSci = 0xFFF0;

// HC11 vectors, lowest priority first.
namespace hc11 {
constexpr u16 kVecSci = 0xFFD6, kVecSpi = 0xFFD8, kVecPai = 0xFFDA, kVecPaov = 0xFFDC, kVecTof = 0xFFDE,
              kVecOc5 = 0xFFE0, kVecOc4 = 0xFFE2, kVecOc3 = 0xFFE4, kVecOc2 = 0xFFE6, kVecOc1 = 0xFFE8,
              kVecIc3 = 0xFFEA, kVecIc2 = 0xFFEC, kVecIc1 = 0xFFEE, kVecRti = 0xFFF0, kVecIrq = 0xFFF2,
              kVecXirq = 0xFFF4, kVecSwi = 0xFFF6, kVecIllop = 0xFFF8, kVecCop = 0xFFFA, kVecCme = 0xFFFC,
              kVecReset = 0xFFFE;
}  // namespace hc11

// Register field.
constexpr u16 kP1Ddr = 0x00, kP2Ddr = 0x01, kP1Data = 0x02, kP2Data = 0x03, kP3Ddr = 0x04, kP4Ddr = 0x05,
              kP3Data = 0x06, kP4Data = 0x07, kTcsr = 0x08, kFrcH = 0x09, kFrcL = 0x0A, kOcrH = 0x0B, kOcrL = 0x0C,
              kIcrH = 0x0D, kIcrL = 0x0E, kP3Csr = 0x0F, kRmcr = 0x10, kTrcsr = 0x11, kRdr = 0x12, kTdr = 0x13,
              kRamCtrl = 0x14;
constexpr u16 kRegFieldSize = 0x20;
constexpr u16 kRamBase = 0x80, kRamSize = 0x80;

// Register-field bytes that are decoded as external addresses in `mode`
// (bit n = $00n): the ports that become the address and data bus in the
// expanded modes. MC6801 reference manual figure 2-17, HD6301V1 manual
// table 2-2-1.
constexpr u32 external_registers(Family f, int mode) {
  constexpr u32 p1 = (1u << kP1Ddr) | (1u << kP1Data);
  constexpr u32 p3 = (1u << kP3Ddr) | (1u << kP3Data) | (1u << kP3Csr);
  constexpr u32 p4 = (1u << kP4Ddr) | (1u << kP4Data);
  if (f == Family::M6801) return (mode == 4 || mode == 7 ? 0 : p3) | (mode <= 3 ? p4 : 0);
  return (mode == 1 ? p1 : 0) | (mode == 7 ? 0 : p3) | (mode <= 2 || mode == 4 ? p4 : 0);
}
// Internal RAM present in `mode` (the MC6801 has none in mode 3).
constexpr bool has_internal_ram(Family f, int mode) { return f == Family::HD6301 || mode != 3; }

}  // namespace m6801

// Hitachi H8/300H (H8/3002) core: shared types.
//
// The H8/3002 is the microcontroller of the JP-8000 / JP-8080:
// H8/300H CPU (advanced mode, 24-bit addresses), on-chip RAM, ITU, TPC,
// two SCIs, DMAC, WDT, refresh controller, A/D converter and the ports.
#pragma once
#include "common/types.hpp"

namespace h8300h {

using emu::s16;
using emu::s32;
using emu::s64;
using emu::s8;
using emu::u16;
using emu::u32;
using emu::u64;
using emu::u8;

constexpr u32 kAddrMask = 0xFFFFFF;

// CCR bits.
constexpr u8 kCcrC = 0x01, kCcrV = 0x02, kCcrZ = 0x04, kCcrN = 0x08, kCcrU = 0x10, kCcrH = 0x20, kCcrUi = 0x40, kCcrI = 0x80;

// Exception / interrupt vector numbers (vector table at 0, four bytes each).
enum Vector : u8 {
  kVecReset = 0,
  kVecNmi = 7,
  kVecTrap0 = 8,
  kVecIrq0 = 12, kVecIrq1 = 13, kVecIrq2 = 14, kVecIrq3 = 15, kVecIrq4 = 16, kVecIrq5 = 17,
  kVecWovi = 20, kVecCmi = 21,
  kVecImia0 = 24, kVecImib0 = 25, kVecOvi0 = 26,
  kVecImia1 = 28, kVecImib1 = 29, kVecOvi1 = 30,
  kVecImia2 = 32, kVecImib2 = 33, kVecOvi2 = 34,
  kVecImia3 = 36, kVecImib3 = 37, kVecOvi3 = 38,
  kVecImia4 = 40, kVecImib4 = 41, kVecOvi4 = 42,
  kVecDend0a = 44, kVecDend0b = 45, kVecDend1a = 46, kVecDend1b = 47,
  kVecEri0 = 52, kVecRxi0 = 53, kVecTxi0 = 54, kVecTei0 = 55,
  kVecEri1 = 56, kVecRxi1 = 57, kVecTxi1 = 58, kVecTei1 = 59,
  kVecAdi = 60,
  kVectorCount = 64,
};

// Register-file addresses (advanced mode).
constexpr u32 kRegFieldBase = 0xFFFF00;      // FFFF00h-FFFFFFh: the on-chip register field (FFFF1Ch up)
constexpr u32 kRamBase = 0xFFFD10, kRamEnd = 0xFFFF0F;  // 512 bytes of on-chip RAM
constexpr u32 kAbwcr = 0xFFFFEC, kAstcr = 0xFFFFED, kWcr = 0xFFFFEE, kWcer = 0xFFFFEF;
constexpr u32 kMdcr = 0xFFFFF1, kSyscr = 0xFFFFF2, kBrcr = 0xFFFFF3, kIscr = 0xFFFFF4, kIer = 0xFFFFF5, kIsr = 0xFFFFF6;
constexpr u32 kIpra = 0xFFFFF8, kIprb = 0xFFFFF9;

// Bus access classes (FlatBus AccessClass slots): the eight external areas
// selected by ABWCR / ASTCR / WCR / WCER, the on-chip RAM (two states per
// word) and the register field (three states per byte, or per word for the
// 16-bit registers).
constexpr u8 kClsArea0 = 0, kClsIntRam = 8, kClsRegs = 9;

}  // namespace h8300h

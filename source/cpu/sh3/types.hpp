// Basic types of the SH-3 (SH7709A) emulator core.
#pragma once
#include "cpu/sh/op.hpp"

namespace sh3 {

using emu::u8;
using emu::u16;
using emu::u32;
using emu::u64;
using emu::s8;
using emu::s16;
using emu::s32;
using emu::s64;

// How a range of the address space answers a bus cycle.  The bus keeps a
// table of these, indexed by the 4-bit class in every line's attribute byte,
// so the bus state controller can retime an area by editing one entry.
// Like the SH-4 core, this one charges one state per instruction and the
// penalty of a class per data access (zero for cached memory and on-chip
// resources); the classes exist so a board can make an uncached area cost
// bus cycles.
struct AccessClass {
  u8 width = 32;          // data bus width in bits: 8, 16 or 32
  u8 base = 0;            // extra states per bus cycle (0: overlapped by the pipeline)
  u8 wait = 0;            // wait states per bus cycle
  bool external = false;  // external bus (instruction fetch from here is not cached)

  bool operator==(const AccessClass&) const = default;

  constexpr u32 cycles(unsigned bytes) const {
    const unsigned per = width / 8;
    const unsigned n = bytes > per ? bytes / per : 1;
    return n * u32(base + wait);
  }
};

// SR bits: the family's, plus the SH-3's (it has no FD bit).
using sh::kSrIMask;
using sh::kSrIShift;
using sh::kSrM;
using sh::kSrQ;
using sh::kSrS;
using sh::kSrT;
constexpr u32 kSrBL = 1u << 28, kSrRB = 1u << 29, kSrMD = 1u << 30;
constexpr u32 kSrMask = kSrMD | kSrRB | kSrBL | kSrM | kSrQ | kSrIMask | kSrS | kSrT;

// Exception event codes (EXPEVT / INTEVT / INTEVT2), SH7709A hardware manual
// table 4.x.  The general-exception ones are re-executable except TRAPA.
constexpr u32 kExcPowerOn = 0x000, kExcManualReset = 0x020;
constexpr u32 kExcTlbMissRead = 0x040, kExcTlbMissWrite = 0x060, kExcInitialPageWrite = 0x080;
constexpr u32 kExcTlbProtRead = 0x0A0, kExcTlbProtWrite = 0x0C0;
constexpr u32 kExcAddrErrRead = 0x0E0, kExcAddrErrWrite = 0x100;
constexpr u32 kExcTrapa = 0x160, kExcIllegal = 0x180, kExcSlotIllegal = 0x1A0;
constexpr u32 kExcNmi = 0x1C0, kExcUserBreak = 0x1E0;
// Vector offsets from VBR.
constexpr u32 kVecGeneral = 0x100, kVecTlbMiss = 0x400, kVecInterrupt = 0x600;

}  // namespace sh3

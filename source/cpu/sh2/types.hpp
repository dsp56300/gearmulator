// Basic types of the SH-2 (SH7014/16/17) emulator core.
#pragma once
#include "common/types.hpp"

namespace sh2 {

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
// so the bus state controller can retime an area (wait states, bus width) by
// editing one entry instead of touching every line.
struct AccessClass {
  u8 width = 32;        // data bus width in bits: 8, 16 or 32
  u8 base = 1;          // states per bus cycle before wait states (peripheral registers: 2 or 3)
  u8 wait = 0;          // software/hardware wait states per bus cycle
  bool external = false;  // fetched through the external bus (instruction fetch costs bus cycles)
  bool cacheable = false; // CS / DRAM space: subject to the instruction cache

  bool operator==(const AccessClass&) const = default;

  // States taken by one access of `bytes` bytes.
  constexpr u32 cycles(unsigned bytes) const {
    const unsigned per = width / 8;
    const unsigned n = bytes > per ? bytes / per : 1;
    return n * u32(base + wait);
  }
};

}  // namespace sh2

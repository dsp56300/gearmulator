// SH-2 address space: the family's paged bus (common/pagedbus.hpp) with the
// SH7014 / SH7034 class table and the instruction-fetch costs the SH-2
// charges on top of the data-access penalty.
//
// A chip that decodes two windows (SH7014: CS0-CS3 / DRAM at the bottom,
// on-chip registers and RAM at H'FFFF8000) and one that scatters small
// regions over eight 16 MB areas (SH7034) cost the same, since only the
// pages something is mapped into get backing memory.  Unmapped pages read
// H'FF, drop writes and raise an address error when fetched from.
#pragma once
#include "common/pagedbus.hpp"
#include "cpu/sh2/types.hpp"

namespace sh2 {

using Device = emu::Device;
using CodeSink = emu::CodeSink;

class Bus;

struct BusPolicy {
  using AccessClass = sh2::AccessClass;
  static constexpr bool kLittleEndian = false;
  // A data access stalls the pipeline by its bus cycles beyond the one the
  // MA stage overlaps.
  static constexpr u32 penalty(u32 cycles) { return cycles > 1 ? cycles - 1 : 0; }
};

class Bus final : public emu::PagedBus<Bus, BusPolicy> {
 public:
  static constexpr u32 kOnchipBase = 0xFFFF8000u;
  static constexpr u32 kOnchipSize = 0x8000u;

  // Default access classes (boards may redefine them with set_class).
  enum Class : u8 {
    kClsOnchipRam = 0,  // 32-bit, 1 state
    kClsOnchipRom = 1,  // 32-bit, 1 state
    kClsPeriph8 = 2,    // 8-bit peripheral registers, 2 states (SCI)
    kClsPeriph16 = 3,   // 16-bit peripheral registers, 2 states
    kClsPeriph16S3 = 4, // 16-bit peripheral registers, 3 states (WDT, DMAC, CAC)
    kClsCs0 = 5,
    kClsCs1 = 6,
    kClsCs2 = 7,
    kClsCs3 = 8,
    kClsDram = 9,
    kClsArea5 = 10,  // SH7034 external areas 5-7 (the SH7014 has no use for them)
    kClsArea6 = 11,
    kClsArea7 = 12,
    kClsUnmapped = 15,
  };

  Bus() {
    classes_[kClsOnchipRam] = {32, 1, 0, false, false};
    classes_[kClsOnchipRom] = {32, 1, 0, false, false};
    classes_[kClsPeriph8] = {8, 2, 0, false, false};
    classes_[kClsPeriph16] = {16, 2, 0, false, false};
    classes_[kClsPeriph16S3] = {16, 3, 0, false, false};
    // Ordinary-space and DRAM bus cycles take 2 states before wait states (figure 8.3).
    for (unsigned c = kClsCs0; c <= kClsArea7; ++c) classes_[c] = {16, 2, 0, true, true};
    classes_[kClsUnmapped] = {16, 2, 0, true, false};
    retime_all();
  }

  // ---- timing derived from the class table -------------------------------------
  // Bus states to fetch one 4-byte instruction line, and the part of it two
  // one-state instructions cannot hide.
  u32 fetch_fill(u8 cls) const { return fetch_fill_[cls & kClassMask]; }
  u32 fetch_static(u8 cls) const { return fetch_static_[cls & kClassMask]; }
  bool cacheable(u8 attr) const { return (cacheable_mask_ >> (attr & kClassMask)) & 1; }

 private:
  friend class emu::PagedBus<Bus, BusPolicy>;
  void on_retime(unsigned i) {
    const AccessClass& k = classes_[i];
    const u32 fill = k.cycles(4);
    fetch_fill_[i] = u16(fill);
    fetch_static_[i] = u16(fill > 2 ? fill - 2 : 0);
    const u16 bit = u16(1u << i);
    cacheable_mask_ = k.cacheable ? u16(cacheable_mask_ | bit) : u16(cacheable_mask_ & ~bit);
  }

  u16 fetch_fill_[16] = {}, fetch_static_[16] = {};
  u16 cacheable_mask_ = 0;
};

}  // namespace sh2

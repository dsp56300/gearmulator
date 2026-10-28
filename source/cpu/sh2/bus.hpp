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

  // BSC page-mode history belongs to bus transactions, not to instructions:
  // on-chip accesses and cache hits leave the external DRAM row alone.
  void configure_dram(unsigned row_bits, bool ras_down, int write_wait_delta) {
    dram_row_bits_ = row_bits;
    dram_ras_down_ = ras_down;
    dram_write_wait_delta_ = write_wait_delta;
    close_dram_row();
    update_row_closers();
    update_dram_costs();
  }
  void close_dram_row() { dram_row_ = 0xFFFFFFFFu; }
  u32 access_cycles(u32 address, u8 attr, unsigned bytes, bool write = false) {
    const unsigned cls = attr & kClassMask;
    const auto& k = classes_[cls];
    u32 cycles = k.cycles(bytes);
    if (!k.external) return cycles;
    if (cls != kClsDram) {
      if (!dram_ras_down_) close_dram_row();
      return cycles;
    }
    const unsigned beats = std::max(1u, bytes / (k.width / 8));
    if (write) cycles = u32(s32(cycles) + s32(beats) * dram_write_wait_delta_);
    const u32 row = address >> dram_row_bits_;
    if (k.burst && row == dram_row_)
      cycles -= k.base + k.wait - k.burst;
    dram_row_ = k.burst ? row : 0xFFFFFFFFu;
    return cycles;
  }
  // The same as access_cycles, from tables: every class but DRAM costs what
  // its tables say and at most closes the open row, DRAM looks up the cost of
  // a row hit or miss.
  u32 access_penalty(u32 address, u8 attr, unsigned log2_bytes, bool write = false) {
    const unsigned cls = attr & kClassMask;
    if (cls == kClsDram) return dram_cost_[write][log2_bytes][dram_hit(address)].penalty;
    if ((row_closers_ >> cls) & 1) close_dram_row();
    return penalty(attr, log2_bytes);
  }
  // access_cycles of a 4-byte instruction line.
  u32 fetch_cycles(u32 address, u8 cls) {
    cls &= kClassMask;
    if (cls == kClsDram) return dram_cost_[0][2][dram_hit(address)].cycles;
    if ((row_closers_ >> cls) & 1) close_dram_row();
    return fetch_fill_[cls];
  }

 private:
  friend class emu::PagedBus<Bus, BusPolicy>;
  // The row history step of access_cycles for an external DRAM class:
  // whether the access hits the open row, which it then leaves open.
  unsigned dram_hit(u32 address) {
    if (!dram_external_) return 0;
    const u32 row = address >> dram_row_bits_;
    const unsigned hit = dram_burst_ && row == dram_row_;
    dram_row_ = dram_burst_ ? row : 0xFFFFFFFFu;
    return hit;
  }
  void update_dram_costs() {
    const AccessClass& k = classes_[kClsDram];
    dram_external_ = k.external;
    dram_burst_ = k.burst != 0;
    for (unsigned write = 0; write < 2; ++write) {
      for (unsigned lg = 0; lg < 3; ++lg) {
        for (unsigned hit = 0; hit < 2; ++hit) {
          const unsigned bytes = 1u << lg;
          u32 cycles = k.cycles(bytes);
          if (k.external) {
            const unsigned beats = std::max(1u, bytes / (k.width / 8));
            if (write) cycles = u32(s32(cycles) + s32(beats) * dram_write_wait_delta_);
            if (hit) cycles -= k.base + k.wait - k.burst;
          }
          dram_cost_[write][lg][hit] = {cycles, BusPolicy::penalty(cycles)};
        }
      }
    }
  }

  void on_retime(unsigned i) {
    if (i == kClsDram) {
      close_dram_row();
      update_dram_costs();
    }
    const AccessClass& k = classes_[i];
    const u32 fill = k.cycles(4);
    fetch_fill_[i] = u16(fill);
    fetch_static_[i] = u16(fill > 2 ? fill - 2 : 0);
    const u16 bit = u16(1u << i);
    cacheable_mask_ = k.cacheable ? u16(cacheable_mask_ | bit) : u16(cacheable_mask_ & ~bit);
    update_row_closers();
  }
  // The external classes other than DRAM, whose accesses close the open row
  // unless RASD keeps it.
  void update_row_closers() {
    u16 mask = 0;
    for (unsigned c = 0; c < 16; ++c)
      if (c != kClsDram && classes_[c].external) mask = u16(mask | (1u << c));
    row_closers_ = dram_ras_down_ ? u16(0) : mask;
  }

  u16 fetch_fill_[16] = {}, fetch_static_[16] = {};
  u16 cacheable_mask_ = 0;
  u16 row_closers_ = 0;
  struct DramCost { u32 cycles, penalty; };
  DramCost dram_cost_[2][3][2] = {};  // [write][log2 bytes][row hit]
  bool dram_external_ = false, dram_burst_ = false;
  u32 dram_row_ = 0xFFFFFFFFu;
  unsigned dram_row_bits_ = 9;
  bool dram_ras_down_ = false;
  int dram_write_wait_delta_ = 0;
};

}  // namespace sh2

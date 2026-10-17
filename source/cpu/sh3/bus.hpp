// SH-3 address space (big-endian): the family's paged bus
// (common/pagedbus.hpp) with the SH-3's view of the physical space.
//
// The SH-3's physical space is 512 MB (29 bits).  P0 (00000000-7FFFFFFF),
// P1 (80000000-9FFFFFFF, cached) and P2 (A0000000-BFFFFFFF, uncached) all
// show that space; P3 (C0000000-DFFFFFFF) too with the MMU off; P4
// (E0000000-FFFFFFFF) holds the on-chip control registers.  map_physical()
// maps a range once and aliases the pages of the mirrors, so the CPU never
// masks addresses.  Pages nobody maps consult the unmapped device (a board's
// catch-all), or read all ones.
#pragma once
#include "common/pagedbus.hpp"
#include "cpu/sh3/types.hpp"

namespace sh3 {

using Device = emu::Device;
using CodeSink = emu::CodeSink;

class Bus;

struct BusPolicy {
  using AccessClass = sh3::AccessClass;
  static constexpr bool kLittleEndian = false;
  // A data access stalls the pipeline by the whole of its class's cycles:
  // the classes start at zero and a board adds what its areas cost.
  static constexpr u32 penalty(u32 cycles) { return cycles; }
};

class Bus final : public emu::PagedBus<Bus, BusPolicy> {
  using Base = emu::PagedBus<Bus, BusPolicy>;

 public:
  static constexpr u32 kPhysMask = 0x1FFFFFFFu;

  // Default access classes (boards may redefine them with set_class).
  enum Class : u8 {
    kClsFast = 0,      // cached memory, on-chip resources: no penalty
    kClsArea0 = 1,     // external areas 0-6 (CS0-CS6)
    kClsArea1 = 2,
    kClsArea2 = 3,
    kClsArea3 = 4,
    kClsArea4 = 5,
    kClsArea5 = 6,
    kClsArea6 = 7,
    kClsRegs = 8,      // on-chip registers
    kClsUnmapped = 15,
  };

  Bus() {
    for (unsigned c = kClsArea0; c <= kClsArea6; ++c) classes_[c] = {32, 0, 0, true};
    classes_[kClsUnmapped] = {32, 0, 0, true};
    retime_all();
  }

  // The 512 MB physical space appears seven times: four times in P0 (the
  // MMU is off, so bits 29-30 are ignored there) and once each in P1, P2
  // and P3.  region_base() gives the base of alias `region` (0-6).
  static constexpr unsigned kRegions = 7;
  static constexpr u32 region_base(unsigned region) { return u32(region) << 29; }

  // ---- mapping ------------------------------------------------------------
  // Physical-space mappings: the range is mapped at `phys` (< 512 MB) and
  // aliased in P0, P1, P2 and P3.
  void map_physical_ram(u32 phys, u32 size, u8 cls = kClsFast) { map_phys(phys, size, Line::Ram, nullptr, cls); }
  void map_physical_rom(u32 phys, u32 size, u8 cls = kClsFast) { map_phys(phys, size, Line::Rom, nullptr, cls); }
  void map_physical_device(u32 phys, u32 size, Device* dev, u8 cls = kClsFast) { map_phys(phys, size, Line::Dev, dev, cls); }
  // ROM whose reads come straight from memory but whose writes reach `dev`
  // (a flash chip's command interface).
  void map_physical_rom_device(u32 phys, u32 size, Device* dev, u8 cls = kClsFast) { map_phys(phys, size, Line::RomDev, dev, cls); }
  // Whole physical pages showing the memory mapped at `target` (a chip
  // select decoded on fewer address lines than its window).
  void mirror_physical(u32 phys, u32 size, u32 target) {
    for (u32 region = 0; region < kRegions; ++region) mirror(region_base(region) | phys, size, region_base(region) | target);
  }
  // Direct (architectural-address) mappings for the P4 space.
  using Base::map_device;
  using Base::map_ram;
  using Base::map_rom;
  using Base::map_rom_device;
  void map_ram(u32 base, u32 size) { Base::map_ram(base, size, kClsFast); }
  void map_rom(u32 base, u32 size) { Base::map_rom(base, size, kClsFast); }
  void map_device(u32 base, u32 size, Device* dev) { Base::map_device(base, size, dev, kClsRegs); }

 private:
  // Map on P0, then alias the pages the range touches in the other regions.
  void map_phys(u32 phys, u32 size, Line::Kind kind, Device* dev, u8 cls) {
    phys &= kPhysMask;
    map(phys, size, kind, dev, cls);
    const u32 first = phys & ~(kPageSize - 1);
    const u32 last = u32((u64(phys) + size - 1) & ~u64(kPageSize - 1));
    const u32 span = last - first + kPageSize;
    for (u32 region = 1; region < kRegions; ++region) mirror(region_base(region) | first, span, first);
  }
};

}  // namespace sh3

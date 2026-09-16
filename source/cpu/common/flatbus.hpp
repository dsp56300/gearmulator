// Flat address space / bus model shared by the ported threaded-code cores.
//
// The whole guest address space is ONE flat byte array (16 MB at most: these
// are 16- to 24-bit machines).  RAM and ROM live directly in it, so a data
// access is
//
//     attr = attr_[addr >> 7];            // one byte per 128-byte line
//     if (attr & kRdSlow) -> device      // rare
//     else                 -> mem_[addr]
//
// which is the h8500 bus with the sh2 attribute layout: a 4-bit access class
// (whose meaning the core defines through a table of AccessClass entries, for
// cycle accounting) plus the slow-path, no-execute and code flags.
//
// Memory-mapped I/O is handled by Device objects attached to lines.  Writes to
// ROM, unmapped space or RAM lines that hold decoded instructions take the
// slow path (the latter so the CPU can drop stale instruction-cache cells).
// Retiming an access class notifies the CodeSink so decoded cells that baked
// the old fetch cost in can be dropped.
#pragma once
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

#include "common/device.hpp"
#include "common/types.hpp"

namespace emu {

// Timing of one access class.  cycles(bytes) = ceil(bytes / bus_bytes) *
// (states + waits); a core may consult the fields directly instead.
struct AccessClass {
  u8 bus_bytes = 2;   // data bus width in bytes (1, 2 or 4)
  u8 states = 2;      // bus states per bus cycle
  u8 waits = 0;       // wait states per bus cycle
  bool external = false;
  bool operator==(const AccessClass& o) const {
    return bus_bytes == o.bus_bytes && states == o.states && waits == o.waits && external == o.external;
  }
  u32 cycles(unsigned bytes) const { return ((bytes + bus_bytes - 1) / bus_bytes) * u32(states + waits); }
};

template <unsigned AddrBits, bool LittleEndian>
class FlatBus {
 public:
  static_assert(AddrBits >= 16 && AddrBits <= 24);
  static constexpr unsigned kAddrBits = AddrBits;
  static constexpr u32 kAddrMask = (1u << AddrBits) - 1;
  static constexpr bool kLittleEndian = LittleEndian;
  static constexpr unsigned kLineShift = 7;  // 128-byte lines
  static constexpr u32 kLineSize = 1u << kLineShift;
  static constexpr u32 kLines = (kAddrMask + 1) >> kLineShift;

  // Line attribute bits.
  static constexpr u8 kClassMask = 0x0F;  // index into the AccessClass table
  static constexpr u8 kRdSlow = 0x10;     // reads go through a Device
  static constexpr u8 kWrSlow = 0x20;     // writes need the slow path (device / ROM / unmapped / code line)
  static constexpr u8 kNoExec = 0x40;     // instruction fetch from here is an error
  static constexpr u8 kCode = 0x80;       // RAM line containing decoded instructions
  static constexpr u8 kClsUnmapped = 15;

  struct Line {
    enum Kind : u8 { Unmapped = 0, Ram, Rom, Dev, RomDev } kind = Unmapped;
    Device* dev = nullptr;
  };

  FlatBus() : mem_(size_t(kAddrMask) + 1, 0xFF), lines_(kLines), attr_(kLines, u8(kWrSlow | kRdSlow | kClsUnmapped)) {
    for (unsigned c = 0; c < classes_.size(); ++c) classes_[c] = AccessClass{};
  }

  static constexpr u32 addr_mask() { return kAddrMask; }
  size_t size() const { return mem_.size(); }

  // --- mapping (128-byte granularity) ---------------------------------------
  void map_ram(u32 base, u32 size, u8 cls = 0) { map(base, size, Line::Ram, nullptr, cls); }
  void map_rom(u32 base, u32 size, u8 cls = 0) { map(base, size, Line::Rom, nullptr, cls); }
  void map_device(u32 base, u32 size, Device* dev, u8 cls = 0) { map(base, size, Line::Dev, dev, cls); }
  // Return a range to the unmapped state (reads open bus, writes dropped).
  void unmap(u32 base, u32 size) { map(base, size, Line::Unmapped, nullptr, kClsUnmapped); }
  // Attributes of space nothing is mapped at: `dev` answers reads and writes
  // there (nullptr: reads H'FF, writes dropped).
  Device* unmapped_device() const { return unmapped_dev_; }
  void set_unmapped_device(Device* dev) {
    unmapped_dev_ = dev;
    for (size_t i = 0; i < lines_.size(); ++i)
      if (lines_[i].kind == Line::Unmapped) lines_[i].dev = dev;
  }
  void set_noexec(u32 base, u32 size, bool noexec = true) {
    for (u32 l = base >> kLineShift; l < (base + size) >> kLineShift && l < kLines; ++l)
      attr_[l] = noexec ? u8(attr_[l] | kNoExec) : u8(attr_[l] & ~kNoExec);
  }
  // Fill the backing store (works for ROM too; use it to load images).
  void load(u32 base, const u8* data, size_t n) {
    base &= kAddrMask;
    if (base + n > mem_.size()) n = mem_.size() - base;
    std::memcpy(&mem_[base], data, n);
  }

  u8* mem() { return mem_.data(); }
  const u8* mem() const { return mem_.data(); }
  u8* ptr(u32 a) { return &mem_[a & kAddrMask]; }

  // --- access classes ---------------------------------------------------------
  // Retiming a class drops the decoded code that baked its fetch cost in.
  void set_class(unsigned idx, const AccessClass& c) {
    AccessClass& k = classes_[idx & kClassMask];
    if (k == c) return;
    k = c;
    if (sink_) sink_->code_timing_changed();
  }
  const AccessClass& access_class(unsigned idx) const { return classes_[idx & kClassMask]; }
  const AccessClass& access_class_of(u8 attr) const { return classes_[attr & kClassMask]; }
  typename Line::Kind line_kind(u32 a) const { return line(a & kAddrMask).kind; }
  Device* device_at(u32 a) const { return line(a & kAddrMask).dev; }

  // --- per-line queries (address must already be masked) --------------------
  u8 attr(u32 a) const { return attr_[a >> kLineShift]; }
  // A wide access whose bytes fall into two lines.
  static constexpr bool crosses_line(u32 a, unsigned n) { return ((a & (kLineSize - 1)) + n) > kLineSize; }
  bool executable(u32 a) const { return (attr(a & kAddrMask) & kNoExec) == 0; }

  // --- generic access -------------------------------------------------------
  u8 read8(u32 a) const {
    a &= kAddrMask;
    return (attr(a) & kRdSlow) ? slow_read8(a) : mem_[a];
  }
  u16 read16(u32 a) const {
    a &= kAddrMask;
    return (attr(a) & kRdSlow) || crosses_line(a, 2) ? slow_read16(a) : get16(&mem_[a]);
  }
  u32 read32(u32 a) const {
    a &= kAddrMask;
    return (attr(a) & kRdSlow) || crosses_line(a, 4) ? slow_read32(a) : get32(&mem_[a]);
  }
  void write8(u32 a, u8 v) {
    a &= kAddrMask;
    if (attr(a) & kWrSlow) slow_write8(a, v); else mem_[a] = v;
  }
  void write16(u32 a, u16 v) {
    a &= kAddrMask;
    if ((attr(a) & kWrSlow) || crosses_line(a, 2)) slow_write16(a, v); else put16(&mem_[a], v);
  }
  void write32(u32 a, u32 v) {
    a &= kAddrMask;
    if ((attr(a) & kWrSlow) || crosses_line(a, 4)) slow_write32(a, v); else put32(&mem_[a], v);
  }

  // --- slow paths (public so the CPU's inlined accessors can reach them) ----
  // A wide access that straddles two lines is split into bytes so each half
  // reaches whatever is mapped there.
  u8 slow_read8(u32 a) const {
    const Line& l = line(a);
    if (l.kind == Line::Dev || l.kind == Line::Unmapped) return l.dev ? l.dev->read8(a) : u8(0xFF);
    return mem_[a];
  }
  u16 slow_read16(u32 a) const {
    if (crosses_line(a, 2)) return compose16(read8(a), read8((a + 1) & kAddrMask));
    const Line& l = line(a);
    if (l.kind == Line::Dev || l.kind == Line::Unmapped) return l.dev ? l.dev->read16(a) : u16(0xFFFF);
    return get16(&mem_[a]);
  }
  u32 slow_read32(u32 a) const {
    if (crosses_line(a, 4)) return compose32(read16(a), read16((a + 2) & kAddrMask));
    const Line& l = line(a);
    if (l.kind == Line::Dev || l.kind == Line::Unmapped) return l.dev ? l.dev->read32(a) : 0xFFFFFFFFu;
    return get32(&mem_[a]);
  }
  void slow_write8(u32 a, u8 v) {
    Line& l = line(a);
    switch (l.kind) {
      case Line::Dev: case Line::RomDev: l.dev->write8(a, v); break;
      case Line::Ram: mem_[a] = v; code_written(a); break;
      case Line::Unmapped: if (l.dev) l.dev->write8(a, v); break;
      default: break;  // ROM: ignored
    }
  }
  void slow_write16(u32 a, u16 v) {
    if (crosses_line(a, 2)) { write8(a, lo_byte16(v)); write8((a + 1) & kAddrMask, hi_byte16(v)); return; }
    Line& l = line(a);
    switch (l.kind) {
      case Line::Dev: case Line::RomDev: l.dev->write16(a, v); break;
      case Line::Ram: put16(&mem_[a], v); code_written(a); break;
      case Line::Unmapped: if (l.dev) l.dev->write16(a, v); break;
      default: break;
    }
  }
  void slow_write32(u32 a, u32 v) {
    if (crosses_line(a, 4)) { write16(a, lo_half32(v)); write16((a + 2) & kAddrMask, hi_half32(v)); return; }
    Line& l = line(a);
    switch (l.kind) {
      case Line::Dev: case Line::RomDev: l.dev->write32(a, v); break;
      case Line::Ram: put32(&mem_[a], v); code_written(a); break;
      case Line::Unmapped: if (l.dev) l.dev->write32(a, v); break;
      default: break;
    }
  }

  // --- instruction-cache cooperation ----------------------------------------
  void set_code_sink(CodeSink* s) { sink_ = s; }
  // Called by the CPU when it decodes an instruction at `a`: RAM lines become
  // write-slow so that later writes can invalidate the decoded cells.
  void mark_code(u32 a) {
    a &= kAddrMask;
    if (line(a).kind == Line::Ram) attr_[a >> kLineShift] |= u8(kCode | kWrSlow);
  }
  void unmark_code(u32 a) {
    a &= kAddrMask;
    if (line(a).kind == Line::Ram) attr_[a >> kLineShift] &= u8(~(kCode | kWrSlow));
  }

  // --- endian helpers ---------------------------------------------------------
  static u16 get16(const u8* p) {
    if constexpr (LittleEndian) return u16(p[0] | (u16(p[1]) << 8));
    else return u16((u16(p[0]) << 8) | p[1]);
  }
  static void put16(u8* p, u16 v) {
    if constexpr (LittleEndian) { p[0] = u8(v); p[1] = u8(v >> 8); }
    else { p[0] = u8(v >> 8); p[1] = u8(v); }
  }
  static u32 get32(const u8* p) {
    if constexpr (LittleEndian) return u32(get16(p)) | (u32(get16(p + 2)) << 16);
    else return (u32(get16(p)) << 16) | get16(p + 2);
  }
  static void put32(u8* p, u32 v) {
    if constexpr (LittleEndian) { put16(p, u16(v)); put16(p + 2, u16(v >> 16)); }
    else { put16(p, u16(v >> 16)); put16(p + 2, u16(v)); }
  }
  // Byte at the lower address of a 16-bit value, and the other one.
  static constexpr u8 lo_byte16(u16 v) { return LittleEndian ? u8(v) : u8(v >> 8); }
  static constexpr u8 hi_byte16(u16 v) { return LittleEndian ? u8(v >> 8) : u8(v); }
  static constexpr u16 compose16(u8 at_a, u8 at_a1) {
    return LittleEndian ? u16(at_a | (u16(at_a1) << 8)) : u16((u16(at_a) << 8) | at_a1);
  }
  static constexpr u16 lo_half32(u32 v) { return LittleEndian ? u16(v) : u16(v >> 16); }
  static constexpr u16 hi_half32(u32 v) { return LittleEndian ? u16(v >> 16) : u16(v); }
  static constexpr u32 compose32(u16 at_a, u16 at_a2) {
    return LittleEndian ? (u32(at_a) | (u32(at_a2) << 16)) : ((u32(at_a) << 16) | at_a2);
  }

 private:
  const Line& line(u32 a) const { return lines_[a >> kLineShift]; }
  Line& line(u32 a) { return lines_[a >> kLineShift]; }

  void map(u32 base, u32 size, typename Line::Kind kind, Device* dev, u8 cls) {
    assert((base & (kLineSize - 1)) == 0 && "region base must be 128-byte aligned");
    assert((size & (kLineSize - 1)) == 0 && "region size must be a multiple of 128");
    assert(u64(base) + size - 1 <= kAddrMask && "region outside address space");
    u8 at = u8(cls & kClassMask);
    if (kind == Line::Dev || kind == Line::Unmapped) at |= kRdSlow | kWrSlow;
    if (kind == Line::Rom || kind == Line::RomDev) at |= kWrSlow;
    for (u32 l = base >> kLineShift; l < (base + size) >> kLineShift; ++l) {
      lines_[l].kind = kind;
      lines_[l].dev = kind == Line::Unmapped ? unmapped_dev_ : dev;
      attr_[l] = u8(at | (attr_[l] & kNoExec));
    }
    if (kind == Line::Ram) std::memset(&mem_[base], 0, size);
  }
  void code_written(u32 a) {
    if (sink_ && (attr(a) & kCode)) sink_->code_line_written(a);
  }

  std::vector<u8> mem_;
  std::vector<Line> lines_;
  std::vector<u8> attr_;
  std::array<AccessClass, 16> classes_{};
  CodeSink* sink_ = nullptr;
  Device* unmapped_dev_ = nullptr;
};

}  // namespace emu

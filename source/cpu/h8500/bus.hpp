// Address space / bus model.
//
// The whole guest address space (64 KB in minimum mode, 16 MB in maximum mode)
// is backed by ONE flat byte array.  RAM and ROM live directly in it, so the
// common case of a data access is:
//
//     attr = attr_[addr >> 7];            // one byte per 128-byte line
//     if (attr & kRdSlow) -> device      // rare
//     else                 -> mem_[addr]
//
// The per-line attribute byte also carries the bus class (width / access
// states) and the wait-state count needed for cycle accounting, so the CPU
// gets everything it needs about an access from a single load.
//
// Memory-mapped I/O is handled by Device objects attached to lines.  Writes to
// ROM, unmapped space or RAM lines that hold decoded instructions also take
// the slow path (the latter so the CPU can drop stale instruction-cache cells).
#pragma once
#include <cassert>
#include <cstring>
#include <vector>

#include "common/device.hpp"
#include "cpu/h8500/types.hpp"

namespace h8500 {

using Device = emu::Device;      // read8/write8 required, wider accesses default to byte pairs
using CodeSink = emu::CodeSink;  // notified when a RAM line holding decoded code is written

class Bus {
 public:
  static constexpr unsigned kLineShift = 7;  // 128-byte lines (register field starts at H'FE80)
  static constexpr u32 kLineSize = 1u << kLineShift;

  // Line attribute bits.
  static constexpr u8 kClassMask = 0x03;  // BusClass
  static constexpr u8 kRdSlow = 0x04;     // reads go through a Device
  static constexpr u8 kWrSlow = 0x08;     // writes need the slow path (device / ROM / unmapped / code line)
  static constexpr u8 kNoExec = 0x10;     // instruction prefetch raises an address error
  static constexpr u8 kCode = 0x20;       // RAM line containing decoded instructions
  static constexpr unsigned kWaitShift = 6;  // bits 7-6: wait states (0..3)

  explicit Bus(unsigned addr_bits = 24) : addr_mask_((1u << addr_bits) - 1) {
    const size_t bytes = size_t(addr_mask_) + 1;
    mem_.assign(bytes, 0xFF);  // open bus reads as H'FF
    lines_.assign(bytes >> kLineShift, Line{});
    attr_.assign(bytes >> kLineShift, u8(kWrSlow | u8(BusClass::W16_S3)));
  }

  u32 addr_mask() const { return addr_mask_; }
  size_t size() const { return mem_.size(); }

  // --- mapping (128-byte granularity) ---------------------------------------
  void map_ram(u32 base, u32 size, BusClass cls, u8 wait = 0) { map(base, size, Line::Ram, nullptr, cls, wait); }
  void map_rom(u32 base, u32 size, BusClass cls, u8 wait = 0) { map(base, size, Line::Rom, nullptr, cls, wait); }
  void map_device(u32 base, u32 size, Device* dev, BusClass cls, u8 wait = 0) {
    map(base, size, Line::Dev, dev, cls, wait);
  }
  // Attributes of space nothing is mapped at.
  void set_unmapped(BusClass cls, u8 wait = 0) {
    for (size_t i = 0; i < lines_.size(); ++i)
      if (lines_[i].kind == Line::Unmapped) attr_[i] = u8(kWrSlow | u8(cls) | (wait << kWaitShift));
  }
  void set_noexec(u32 base, u32 size, bool noexec = true) {
    for (u32 l = base >> kLineShift; l < (base + size) >> kLineShift; ++l)
      attr_[l] = noexec ? u8(attr_[l] | kNoExec) : u8(attr_[l] & ~kNoExec);
  }
  // Fill the backing store (works for ROM too; use it to load images).
  void load(u32 base, const u8* data, size_t n) { std::memcpy(&mem_[base & addr_mask_], data, n); }

  u8* mem() { return mem_.data(); }
  const u8* mem() const { return mem_.data(); }

  // --- per-line queries (address must already be masked) --------------------
  u8 attr(u32 a) const { return attr_[a >> kLineShift]; }
  BusClass bus_class(u32 a) const { return BusClass(attr(a & addr_mask_) & kClassMask); }
  u8 wait_states(u32 a) const { return u8(attr(a & addr_mask_) >> kWaitShift); }
  bool executable(u32 a) const { return (attr(a & addr_mask_) & kNoExec) == 0; }

  // --- generic access -------------------------------------------------------
  u8 read8(u32 a) const {
    a &= addr_mask_;
    return (attr(a) & kRdSlow) ? slow_read8(a) : mem_[a];
  }
  u16 read16(u32 a) const {
    a &= addr_mask_ & ~1u;
    return (attr(a) & kRdSlow) ? slow_read16(a) : be16(&mem_[a]);
  }
  void write8(u32 a, u8 v) {
    a &= addr_mask_;
    if (attr(a) & kWrSlow) slow_write8(a, v); else mem_[a] = v;
  }
  void write16(u32 a, u16 v) {
    a &= addr_mask_ & ~1u;
    if (attr(a) & kWrSlow) slow_write16(a, v); else put_be16(&mem_[a], v);
  }

  // --- slow paths (public so the CPU's inlined accessors can reach them) ----
  u8 slow_read8(u32 a) const {
    const Line& l = line(a);
    return l.dev ? l.dev->read8(a) : mem_[a];
  }
  u16 slow_read16(u32 a) const {
    const Line& l = line(a);
    return l.dev ? l.dev->read16(a) : be16(&mem_[a]);
  }
  void slow_write8(u32 a, u8 v) {
    Line& l = line(a);
    switch (l.kind) {
      case Line::Dev: l.dev->write8(a, v); break;
      case Line::Ram: mem_[a] = v; if ((attr(a) & kCode) && sink_) sink_->code_line_written(a); break;
      default: break;  // ROM / unmapped: ignored
    }
  }
  void slow_write16(u32 a, u16 v) {
    Line& l = line(a);
    switch (l.kind) {
      case Line::Dev: l.dev->write16(a, v); break;
      case Line::Ram: put_be16(&mem_[a], v); if ((attr(a) & kCode) && sink_) sink_->code_line_written(a); break;
      default: break;
    }
  }

  // --- instruction-cache cooperation ----------------------------------------
  void set_code_sink(CodeSink* s) { sink_ = s; }
  // Called by the CPU when it decodes an instruction at `a`: RAM lines become
  // write-slow so that later writes can invalidate the decoded cells.
  void mark_code(u32 a) {
    a &= addr_mask_;
    if (line(a).kind == Line::Ram) attr_[a >> kLineShift] |= u8(kCode | kWrSlow);
  }
  void unmark_code(u32 a) {
    a &= addr_mask_;
    if (line(a).kind == Line::Ram) attr_[a >> kLineShift] &= u8(~(kCode | kWrSlow));
  }

  static u16 be16(const u8* p) { return u16((u16(p[0]) << 8) | p[1]); }
  static void put_be16(u8* p, u16 v) { p[0] = u8(v >> 8); p[1] = u8(v); }

 private:
  struct Line {
    enum Kind : u8 { Unmapped = 0, Ram, Rom, Dev } kind = Unmapped;
    Device* dev = nullptr;
  };

  const Line& line(u32 a) const { return lines_[a >> kLineShift]; }
  Line& line(u32 a) { return lines_[a >> kLineShift]; }

  void map(u32 base, u32 size, Line::Kind kind, Device* dev, BusClass cls, u8 wait) {
    assert((base & (kLineSize - 1)) == 0 && "region base must be 128-byte aligned");
    assert((size & (kLineSize - 1)) == 0 && "region size must be a multiple of 128");
    assert(base + size - 1 <= addr_mask_ && "region outside address space");
    assert(wait <= 3);
    u8 at = u8(u8(cls) | (wait << kWaitShift));
    if (kind == Line::Dev) at |= kRdSlow | kWrSlow;
    if (kind == Line::Rom) at |= kWrSlow;
    for (u32 l = base >> kLineShift; l < (base + size) >> kLineShift; ++l) {
      lines_[l].kind = kind;
      lines_[l].dev = dev;
      attr_[l] = u8(at | (attr_[l] & kNoExec));
    }
    if (kind == Line::Ram) std::memset(&mem_[base], 0, size);
  }

  std::vector<u8> mem_;
  std::vector<Line> lines_;
  std::vector<u8> attr_;
  CodeSink* sink_ = nullptr;
  u32 addr_mask_;
};

}  // namespace h8500

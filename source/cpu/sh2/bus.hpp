// SH-2 address space.
//
// The 4 GB architectural space is a table of 64 KB pages.  A page gets
// backing memory when something is mapped into it, so a chip that decodes
// two windows (SH7014: CS0-CS3 / DRAM at the bottom, on-chip registers and
// RAM at H'FFFF8000) and one that scatters small regions over eight 16 MB
// areas (SH7034) cost the same.  Backing is one flat byte array with one
// attribute byte per 128-byte line, exactly like the H8/500 bus, so a data
// access is a page-table load, an attribute load and a branch:
//
//     off  = page_[a >> 16] + (a & H'FFFF)      (page_ = kUnmappedPage: unmapped)
//     attr = attr_[off >> 7]
//     if (attr & kRdSlow) -> device / unmapped, else mem_[off]
//
// The attribute carries a 4-bit access class (see AccessClass) plus the
// slow-path and code flags.  Unmapped pages read H'FF, drop writes and raise
// an address error when fetched from.  mirror() aliases pages for the
// mirrored on-chip areas of the SH-1 parts.
#pragma once
#include <array>
#include <cassert>
#include <algorithm>
#include <cstring>
#include <vector>

#include "common/device.hpp"
#include "cpu/sh2/types.hpp"

namespace sh2 {

using Device = emu::Device;
using CodeSink = emu::CodeSink;

class Bus {
 public:
  static constexpr unsigned kLineShift = 7;
  static constexpr u32 kLineSize = 1u << kLineShift;
  static constexpr u32 kOnchipBase = 0xFFFF8000u;
  static constexpr u32 kOnchipSize = 0x8000u;

  // Attribute bits.
  static constexpr u8 kClassMask = 0x0F;
  static constexpr u8 kRdSlow = 0x10;  // reads go through a Device
  static constexpr u8 kWrSlow = 0x20;  // writes take the slow path (device / ROM / code line)
  static constexpr u8 kNoExec = 0x40;  // instruction fetch raises an address error (peripheral space)
  static constexpr u8 kCode = 0x80;    // RAM line containing decoded instructions

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

  // The address space is a table of 64 KB pages; a page gets backing memory
  // (and a 128-byte-line attribute array) when something is mapped into it.
  static constexpr unsigned kPageShift = 16;
  static constexpr u32 kPageSize = 1u << kPageShift;
  static constexpr u32 kUnmappedPage = 0xFFFFFFFFu;

  Bus() : page_(1u << (32 - kPageShift), kUnmappedPage) {
    classes_[kClsOnchipRam] = {32, 1, 0, false, false};
    classes_[kClsOnchipRom] = {32, 1, 0, false, false};
    classes_[kClsPeriph8] = {8, 2, 0, false, false};
    classes_[kClsPeriph16] = {16, 2, 0, false, false};
    classes_[kClsPeriph16S3] = {16, 3, 0, false, false};
    // Ordinary-space and DRAM bus cycles take 2 states before wait states (figure 8.3).
    for (unsigned c = kClsCs0; c <= kClsArea7; ++c) classes_[c] = {16, 2, 0, true, true};
    classes_[kClsUnmapped] = {16, 2, 0, true, false};
    for (unsigned c = 0; c < classes_.size(); ++c) retime(c);
  }


  // ---- mapping ------------------------------------------------------------
  void map_ram(u32 base, u32 size, u8 cls) { map(base, size, Line::Ram, nullptr, cls); }
  void map_rom(u32 base, u32 size, u8 cls) { map(base, size, Line::Rom, nullptr, cls); }
  void map_device(u32 base, u32 size, Device* dev, u8 cls) { map(base, size, Line::Dev, dev, cls); }
  // ROM whose reads come straight from memory but whose writes reach `dev`
  // (a flash chip's command interface).
  void map_rom_device(u32 base, u32 size, Device* dev, u8 cls) { map(base, size, Line::RomDev, dev, cls); }
  // Alias whole pages: `base` shows the memory mapped at `target` (mirrors).
  void mirror(u32 base, u32 size, u32 target) {
    assert((base & (kPageSize - 1)) == 0 && (size & (kPageSize - 1)) == 0 && (target & (kPageSize - 1)) == 0);
    for (u32 i = 0; i < size >> kPageShift; ++i) page_[(base >> kPageShift) + i] = page_[(target >> kPageShift) + i];
  }
  void set_noexec(u32 base, u32 size) {
    u32 off;
    if (!translate(base, off)) return;
    for (u32 l = off >> kLineShift; l < (off + size) >> kLineShift; ++l) attr_[l] |= kNoExec;
  }
  // Retiming a class drops the decoded code that baked its fetch cost in.
  void set_class(unsigned idx, const AccessClass& c) {
    AccessClass& k = classes_[idx & kClassMask];
    if (k == c) return;
    k = c;
    retime(idx & kClassMask);
    if (sink_) sink_->code_timing_changed();
  }
  const AccessClass& access_class(unsigned idx) const { return classes_[idx & kClassMask]; }
  const AccessClass& access_class_of(u8 attr) const { return classes_[attr & kClassMask]; }

  // ---- timing derived from the class table -------------------------------------
  // States a data access of 1 << log2_bytes bytes stalls the pipeline (bus
  // cycles beyond the one the MA stage overlaps).
  u32 penalty(u8 attr, unsigned log2_bytes) const { return penalty_[attr & kClassMask][log2_bytes]; }
  // Bus states to fetch one 4-byte instruction line, and the part of it two
  // one-state instructions cannot hide.
  u32 fetch_fill(u8 cls) const { return fetch_fill_[cls & kClassMask]; }
  u32 fetch_static(u8 cls) const { return fetch_static_[cls & kClassMask]; }
  bool external(u8 attr) const { return (external_mask_ >> (attr & kClassMask)) & 1; }
  bool cacheable(u8 attr) const { return (cacheable_mask_ >> (attr & kClassMask)) & 1; }
  void set_code_sink(CodeSink* s) { sink_ = s; }

  // Copy an image into memory regardless of the line kind (ROM loading).
  void load(u32 base, const u8* data, size_t n) {
    while (n) {
      u32 off;
      const size_t chunk = std::min<size_t>(n, kPageSize - (base & (kPageSize - 1)));
      if (translate(base, off)) std::memcpy(&mem_[off], data, chunk);
      base += u32(chunk);
      data += chunk;
      n -= chunk;
    }
  }

  // ---- address translation --------------------------------------------------
  // Guest address -> offset into the backing array; false for an unmapped page.
  bool translate(u32 a, u32& off) const {
    const u32 p = page_[a >> kPageShift];
    off = p + (a & (kPageSize - 1));
    return p != kUnmappedPage;
  }
  u8 attr(u32 a) const {
    u32 off;
    return translate(a, off) ? attr_[off >> kLineShift] : u8(kClsUnmapped | kRdSlow | kWrSlow | kNoExec);
  }
  u8 attr_off(u32 off) const { return attr_[off >> kLineShift]; }
  u8* mem() { return mem_.data(); }
  const u8* mem() const { return mem_.data(); }
  // Pointer to guest address `a` (valid up to the end of its 64 KB page and
  // until the next mapping call), or nullptr on an unmapped page.
  u8* ptr(u32 a) {
    u32 off;
    return translate(a, off) ? &mem_[off] : nullptr;
  }

  // ---- access -----------------------------------------------------------------
  u8 read8(u32 a) {
    u32 off;
    if (!translate(a, off)) return 0xFF;
    const u8 at = attr_[off >> kLineShift];
    if (at & kRdSlow) return slow_read8(a, off);
    return mem_[off];
  }
  u16 read16(u32 a) {
    u32 off;
    if (!translate(a, off)) return 0xFFFF;
    const u8 at = attr_[off >> kLineShift];
    if (at & kRdSlow) return slow_read16(a, off);
    return be16(&mem_[off]);
  }
  u32 read32(u32 a) {
    u32 off;
    if (!translate(a, off)) return 0xFFFFFFFFu;
    const u8 at = attr_[off >> kLineShift];
    if (at & kRdSlow) return slow_read32(a, off);
    return be32(&mem_[off]);
  }
  void write8(u32 a, u8 v) {
    u32 off;
    if (!translate(a, off)) return;
    const u8 at = attr_[off >> kLineShift];
    if (at & kWrSlow) { slow_write8(a, off, v); return; }
    mem_[off] = v;
  }
  void write16(u32 a, u16 v) {
    u32 off;
    if (!translate(a, off)) return;
    const u8 at = attr_[off >> kLineShift];
    if (at & kWrSlow) { slow_write16(a, off, v); return; }
    put_be16(&mem_[off], v);
  }
  void write32(u32 a, u32 v) {
    u32 off;
    if (!translate(a, off)) return;
    const u8 at = attr_[off >> kLineShift];
    if (at & kWrSlow) { slow_write32(a, off, v); return; }
    put_be32(&mem_[off], v);
  }

  u8 slow_read8(u32 a, u32 off) {
    const Line& l = lines_[off >> kLineShift];
    return l.kind == Line::Dev ? l.dev->read8(a) : l.kind == Line::Unmapped ? 0xFF : mem_[off];
  }
  u16 slow_read16(u32 a, u32 off) {
    const Line& l = lines_[off >> kLineShift];
    return l.kind == Line::Dev ? l.dev->read16(a) : l.kind == Line::Unmapped ? 0xFFFF : be16(&mem_[off]);
  }
  u32 slow_read32(u32 a, u32 off) {
    const Line& l = lines_[off >> kLineShift];
    return l.kind == Line::Dev ? l.dev->read32(a) : l.kind == Line::Unmapped ? 0xFFFFFFFFu : be32(&mem_[off]);
  }
  void slow_write8(u32 a, u32 off, u8 v) {
    const Line& l = lines_[off >> kLineShift];
    if (l.kind == Line::Dev || l.kind == Line::RomDev) l.dev->write8(a, v);
    else if (l.kind == Line::Ram) { mem_[off] = v; code_written(a); }
  }
  void slow_write16(u32 a, u32 off, u16 v) {
    const Line& l = lines_[off >> kLineShift];
    if (l.kind == Line::Dev || l.kind == Line::RomDev) l.dev->write16(a, v);
    else if (l.kind == Line::Ram) { put_be16(&mem_[off], v); code_written(a); }
  }
  void slow_write32(u32 a, u32 off, u32 v) {
    const Line& l = lines_[off >> kLineShift];
    if (l.kind == Line::Dev || l.kind == Line::RomDev) l.dev->write32(a, v);
    else if (l.kind == Line::Ram) { put_be32(&mem_[off], v); code_written(a); }
  }

  // RAM lines holding decoded code become write-slow so writes can drop cells.
  void mark_code(u32 a) {
    u32 off;
    if (translate(a, off) && lines_[off >> kLineShift].kind == Line::Ram) attr_[off >> kLineShift] |= u8(kCode | kWrSlow);
  }
  void unmark_code(u32 a) {
    u32 off;
    if (translate(a, off) && lines_[off >> kLineShift].kind == Line::Ram) attr_[off >> kLineShift] &= u8(~(kCode | kWrSlow));
  }

  static u16 be16(const u8* p) { return u16((u16(p[0]) << 8) | p[1]); }
  static u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | p[3]; }
  static void put_be16(u8* p, u16 v) { p[0] = u8(v >> 8); p[1] = u8(v); }
  static void put_be32(u8* p, u32 v) { p[0] = u8(v >> 24); p[1] = u8(v >> 16); p[2] = u8(v >> 8); p[3] = u8(v); }

 private:
  struct Line {
    enum Kind : u8 { Unmapped = 0, Ram, Rom, Dev, RomDev } kind = Unmapped;
    Device* dev = nullptr;
  };

  // Give every page touched by [base, base + size) backing memory.
  void ensure_pages(u32 base, u32 size) {
    const u64 end = u64(base) + size;
    for (u64 a = base & ~u64(kPageSize - 1); a < end; a += kPageSize) {
      u32& p = page_[a >> kPageShift];
      if (p != kUnmappedPage) continue;
      p = u32(mem_.size());
      mem_.resize(mem_.size() + kPageSize, 0xFF);
      lines_.resize(lines_.size() + (kPageSize >> kLineShift), Line{});
      attr_.resize(attr_.size() + (kPageSize >> kLineShift), u8(kClsUnmapped | kRdSlow | kWrSlow | kNoExec));
    }
  }
  void map(u32 base, u32 size, Line::Kind kind, Device* dev, u8 cls) {
    assert((base & (kLineSize - 1)) == 0 && (size & (kLineSize - 1)) == 0);
    ensure_pages(base, size);
    u8 at = u8(cls & kClassMask);
    if (kind == Line::Dev) at |= kRdSlow | kWrSlow;
    if (kind == Line::Rom || kind == Line::RomDev) at |= kWrSlow;
    for (u64 a = base; a < u64(base) + size;) {
      u32 off;
      translate(u32(a), off);
      const u32 chunk = u32(std::min<u64>(u64(base) + size - a, kPageSize - (a & (kPageSize - 1))));
      for (u32 l = off >> kLineShift; l < (off + chunk) >> kLineShift; ++l) {
        lines_[l].kind = kind;
        lines_[l].dev = dev;
        attr_[l] = at;
      }
      if (kind == Line::Ram) std::memset(&mem_[off], 0, chunk);
      a += chunk;
    }
  }
  void code_written(u32 a) {
    if (sink_ && (attr(a) & kCode)) sink_->code_line_written(a);
  }
  void retime(unsigned i) {
    const AccessClass& k = classes_[i];
    for (unsigned lg = 0; lg < 3; ++lg) {
      const u32 cyc = k.cycles(1u << lg);
      penalty_[i][lg] = u16(cyc > 1 ? cyc - 1 : 0);
    }
    const u32 fill = k.cycles(4);
    fetch_fill_[i] = u16(fill);
    fetch_static_[i] = u16(fill > 2 ? fill - 2 : 0);
    const u16 bit = u16(1u << i);
    external_mask_ = k.external ? u16(external_mask_ | bit) : u16(external_mask_ & ~bit);
    cacheable_mask_ = k.cacheable ? u16(cacheable_mask_ | bit) : u16(cacheable_mask_ & ~bit);
  }

  std::vector<u32> page_;  // 64 KB page -> offset into mem_, or kUnmappedPage
  std::vector<u8> mem_;
  std::vector<Line> lines_;
  std::vector<u8> attr_;
  std::array<AccessClass, 16> classes_{};
  u16 penalty_[16][3] = {};
  u16 fetch_fill_[16] = {}, fetch_static_[16] = {};
  u16 external_mask_ = 0, cacheable_mask_ = 0;
  CodeSink* sink_ = nullptr;
};

}  // namespace sh2

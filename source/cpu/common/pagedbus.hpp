// Paged address space shared by the 32-bit cores (the SH family).
//
// The 4 GB architectural space is a table of 64 KB pages.  A page gets
// backing memory when something is mapped into it, so a chip that decodes
// two windows and one that scatters small regions over the space cost the
// same.  Backing is one flat byte array with one attribute byte per 128-byte
// line, exactly like FlatBus, so a data access is a page-table load, an
// attribute load and a branch:
//
//     off  = page_[a >> 16] + (a & H'FFFF)      (page_ = kUnmappedPage: unmapped)
//     attr = attr_[off >> 7]
//     if (attr & kRdSlow) -> device / unmapped, else mem_[off]
//
// The attribute carries a 4-bit access class (the Policy's AccessClass, for
// cycle accounting) plus the slow-path, no-execute and code flags.  Pages
// nobody maps consult the unmapped device (a board's catch-all) or read all
// ones and drop writes; an instruction fetch from them is an address error.
// mirror() aliases whole pages, for chip selects decoded on fewer address
// lines than their window and for the mirrored on-chip areas.
//
// Policy:
//   using AccessClass = ...;                 cycles(bytes), external, operator==
//   static constexpr bool kLittleEndian;     byte order of the backing memory
//   static constexpr u32 penalty(u32 cycles);  states a data access of that many
//                                              bus states stalls the pipeline
// Derived (CRTP) may define on_retime(unsigned cls) to keep tables of its
// own in step with the class table; it sets the default classes in its
// constructor and calls retime_all().
#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

#include "common/device.hpp"
#include "common/types.hpp"

namespace emu {

template <class Derived, class Policy>
class PagedBus {
 public:
  using AccessClass = typename Policy::AccessClass;

  static constexpr unsigned kLineShift = 7;
  static constexpr u32 kLineSize = 1u << kLineShift;

  // Attribute bits.
  static constexpr u8 kClassMask = 0x0F;
  static constexpr u8 kRdSlow = 0x10;  // reads go through a Device
  static constexpr u8 kWrSlow = 0x20;  // writes take the slow path (device / ROM / code line)
  static constexpr u8 kNoExec = 0x40;  // instruction fetch raises an address error
  static constexpr u8 kCode = 0x80;    // RAM line containing decoded instructions
  static constexpr u8 kClsUnmapped = 15;

  static constexpr unsigned kPageShift = 16;
  static constexpr u32 kPageSize = 1u << kPageShift;
  static constexpr u32 kUnmappedPage = 0xFFFFFFFFu;

  PagedBus() : page_(1u << (32 - kPageShift), kUnmappedPage) {}

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
  // Device answering for every address no page maps (a board's catch-all).
  void set_unmapped_device(Device* dev) { unmapped_dev_ = dev; }
  Device* unmapped_device() const { return unmapped_dev_; }

  // ---- access classes ---------------------------------------------------------
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

  // States a data access of 1 << log2_bytes bytes stalls the pipeline.
  u32 penalty(u8 attr, unsigned log2_bytes) const { return penalty_[attr & kClassMask][log2_bytes]; }
  bool external(u8 attr) const { return (external_mask_ >> (attr & kClassMask)) & 1; }
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
    if (!translate(a, off)) return unmapped_dev_ ? unmapped_dev_->read8(a) : u8(0xFF);
    const u8 at = attr_[off >> kLineShift];
    if (at & kRdSlow) return slow_read8(a, off);
    return mem_[off];
  }
  u16 read16(u32 a) {
    u32 off;
    if (!translate(a, off)) return unmapped_dev_ ? unmapped_dev_->read16(a) : u16(0xFFFF);
    const u8 at = attr_[off >> kLineShift];
    if (at & kRdSlow) return slow_read16(a, off);
    return get16(&mem_[off]);
  }
  u32 read32(u32 a) {
    u32 off;
    if (!translate(a, off)) return unmapped_dev_ ? unmapped_dev_->read32(a) : 0xFFFFFFFFu;
    const u8 at = attr_[off >> kLineShift];
    if (at & kRdSlow) return slow_read32(a, off);
    return get32(&mem_[off]);
  }
  void write8(u32 a, u8 v) {
    u32 off;
    if (!translate(a, off)) { if (unmapped_dev_) unmapped_dev_->write8(a, v); return; }
    const u8 at = attr_[off >> kLineShift];
    if (at & kWrSlow) { slow_write8(a, off, v); return; }
    mem_[off] = v;
  }
  void write16(u32 a, u16 v) {
    u32 off;
    if (!translate(a, off)) { if (unmapped_dev_) unmapped_dev_->write16(a, v); return; }
    const u8 at = attr_[off >> kLineShift];
    if (at & kWrSlow) { slow_write16(a, off, v); return; }
    put16(&mem_[off], v);
  }
  void write32(u32 a, u32 v) {
    u32 off;
    if (!translate(a, off)) { if (unmapped_dev_) unmapped_dev_->write32(a, v); return; }
    const u8 at = attr_[off >> kLineShift];
    if (at & kWrSlow) { slow_write32(a, off, v); return; }
    put32(&mem_[off], v);
  }

  // Slow paths (public so a core's inlined accessors can reach them).
  u8 slow_read8(u32 a, u32 off) {
    const Line& l = lines_[off >> kLineShift];
    return l.kind == Line::Dev ? l.dev->read8(a) : l.kind == Line::Unmapped ? u8(0xFF) : mem_[off];
  }
  u16 slow_read16(u32 a, u32 off) {
    const Line& l = lines_[off >> kLineShift];
    return l.kind == Line::Dev ? l.dev->read16(a) : l.kind == Line::Unmapped ? u16(0xFFFF) : get16(&mem_[off]);
  }
  u32 slow_read32(u32 a, u32 off) {
    const Line& l = lines_[off >> kLineShift];
    return l.kind == Line::Dev ? l.dev->read32(a) : l.kind == Line::Unmapped ? 0xFFFFFFFFu : get32(&mem_[off]);
  }
  void slow_write8(u32 a, u32 off, u8 v) {
    const Line& l = lines_[off >> kLineShift];
    if (l.kind == Line::Dev || l.kind == Line::RomDev) l.dev->write8(a, v);
    else if (l.kind == Line::Ram) { mem_[off] = v; code_written(a); }
  }
  void slow_write16(u32 a, u32 off, u16 v) {
    const Line& l = lines_[off >> kLineShift];
    if (l.kind == Line::Dev || l.kind == Line::RomDev) l.dev->write16(a, v);
    else if (l.kind == Line::Ram) { put16(&mem_[off], v); code_written(a); }
  }
  void slow_write32(u32 a, u32 off, u32 v) {
    const Line& l = lines_[off >> kLineShift];
    if (l.kind == Line::Dev || l.kind == Line::RomDev) l.dev->write32(a, v);
    else if (l.kind == Line::Ram) { put32(&mem_[off], v); code_written(a); }
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

  // Byte-order helpers: get/put follow the policy, the named ones are fixed.
  static u16 be16(const u8* p) { return u16((u16(p[0]) << 8) | p[1]); }
  static u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | p[3]; }
  static void put_be16(u8* p, u16 v) { p[0] = u8(v >> 8); p[1] = u8(v); }
  static void put_be32(u8* p, u32 v) { p[0] = u8(v >> 24); p[1] = u8(v >> 16); p[2] = u8(v >> 8); p[3] = u8(v); }
  static u16 le16(const u8* p) { return u16(p[0] | (u16(p[1]) << 8)); }
  static u32 le32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }
  static void put_le16(u8* p, u16 v) { p[0] = u8(v); p[1] = u8(v >> 8); }
  static void put_le32(u8* p, u32 v) { p[0] = u8(v); p[1] = u8(v >> 8); p[2] = u8(v >> 16); p[3] = u8(v >> 24); }
  static u16 get16(const u8* p) { return Policy::kLittleEndian ? le16(p) : be16(p); }
  static u32 get32(const u8* p) { return Policy::kLittleEndian ? le32(p) : be32(p); }
  static void put16(u8* p, u16 v) { if constexpr (Policy::kLittleEndian) put_le16(p, v); else put_be16(p, v); }
  static void put32(u8* p, u32 v) { if constexpr (Policy::kLittleEndian) put_le32(p, v); else put_be32(p, v); }

 protected:
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
  void map(u32 base, u32 size, typename Line::Kind kind, Device* dev, u8 cls) {
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
    for (unsigned lg = 0; lg < 3; ++lg) penalty_[i][lg] = u16(Policy::penalty(k.cycles(1u << lg)));
    const u16 bit = u16(1u << i);
    external_mask_ = k.external ? u16(external_mask_ | bit) : u16(external_mask_ & ~bit);
    if constexpr (requires(Derived& d) { d.on_retime(i); }) static_cast<Derived*>(this)->on_retime(i);
  }
  void retime_all() {
    for (unsigned c = 0; c < classes_.size(); ++c) retime(c);
  }

  std::vector<u32> page_;  // 64 KB page -> offset into mem_, or kUnmappedPage
  std::vector<u8> mem_;
  std::vector<Line> lines_;
  std::vector<u8> attr_;
  std::array<AccessClass, 16> classes_{};
  u16 penalty_[16][3] = {};
  u16 external_mask_ = 0;
  CodeSink* sink_ = nullptr;
  Device* unmapped_dev_ = nullptr;
};

}  // namespace emu

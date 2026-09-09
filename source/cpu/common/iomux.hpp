// Register-field multiplexer.
//
// A bus maps devices per line, but an on-chip register field packs many
// peripherals into a few lines.  IoMux is the single Device mapped over the
// register field; peripherals claim byte ranges inside it.  An access as wide
// as a device's claim reaches it whole (registers with word- or longword-only
// protocols see the whole value); otherwise it splits into byte accesses.
#pragma once
#include <vector>

#include "common/device.hpp"

namespace emu {

class IoMux final : public Device {
 public:
  IoMux(u32 base, u32 size) : base_(base), owners_(size, nullptr) {}

  u32 base() const { return base_; }
  u32 size() const { return u32(owners_.size()); }

  // Bytes outside the field are ignored, so a chip that lacks one of a
  // peripheral's registers can pass address 0 for it.
  void assign(u32 addr, u32 len, Device* dev) {
    for (u32 a = addr; a < addr + len; ++a) {
      const u32 off = a - base_;
      if (off < owners_.size()) owners_[off] = dev;
    }
  }
  // Device that answers for bytes no peripheral claims (a board may have RAM
  // behind the unused part of the register field); nullptr reads all ones.
  void set_fallback(Device* dev) { fallback_ = dev; }
  Device* owner(u32 addr) const {
    const u32 off = addr - base_;
    return off < owners_.size() ? owners_[off] : nullptr;
  }

  u8 read8(u32 a) override {
    Device* d = owner(a);
    const u8 v = d ? d->read8(a) : (fallback_ ? fallback_->read8(a) : u8(0xFF));  // unassigned: all ones
    return v;
  }
  void write8(u32 a, u8 v) override {
    Device* d = owner(a);
    if (d) d->write8(a, v);
    else if (fallback_) fallback_->write8(a, v);
  }
  u16 read16(u32 a) override {
    Device* d = owner(a);
    u16 v;
    if (d && d == owner(a + 1)) v = d->read16(a);
    else if (!d && !owner(a + 1) && fallback_) v = fallback_->read16(a);
    else v = Device::read16(a);
    return v;
  }
  void write16(u32 a, u16 v) override {
    Device* d = owner(a);
    if (d && d == owner(a + 1)) d->write16(a, v);
    else if (!d && !owner(a + 1) && fallback_) fallback_->write16(a, v);
    else Device::write16(a, v);
  }
  u32 read32(u32 a) override {
    Device* d = owner(a);
    if (d && d == owner(a + 1) && d == owner(a + 2) && d == owner(a + 3)) return d->read32(a);
    return Device::read32(a);
  }
  void write32(u32 a, u32 v) override {
    Device* d = owner(a);
    if (d && d == owner(a + 1) && d == owner(a + 2) && d == owner(a + 3)) d->write32(a, v);
    else Device::write32(a, v);
  }

 private:
  u32 base_;
  std::vector<Device*> owners_;
  Device* fallback_ = nullptr;
};

}  // namespace emu

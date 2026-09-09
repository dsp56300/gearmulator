// Memory-mapped device interface shared by every bus model.
//
// A device claims a range of a bus and answers byte accesses; wider accesses
// default to big-endian compositions of narrower ones, so a device only has to
// override the widths its registers really support as a unit.
#pragma once
#include "common/types.hpp"

namespace emu {

class Device {
 public:
  virtual ~Device() = default;
  virtual u8 read8(u32 addr) = 0;
  virtual void write8(u32 addr, u8 value) = 0;
  virtual u16 read16(u32 addr) { return u16((u16(read8(addr)) << 8) | read8(addr + 1)); }
  virtual void write16(u32 addr, u16 value) { write8(addr, u8(value >> 8)); write8(addr + 1, u8(value)); }
  virtual u32 read32(u32 addr) { return (u32(read16(addr)) << 16) | read16(addr + 2); }
  virtual void write32(u32 addr, u32 value) { write16(addr, u16(value >> 16)); write16(addr + 2, u16(value)); }
};

// Receives a notification when a RAM line holding decoded instructions is
// written, or when the bus timing that decoded code depends on changes.
class CodeSink {
 public:
  virtual ~CodeSink() = default;
  virtual void code_line_written(u32 addr) = 0;
  virtual void code_timing_changed() {}
};

}  // namespace emu

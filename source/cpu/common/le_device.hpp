// Memory-mapped device interface for little-endian buses.
//
// emu::Device composes wide accesses big-endian, which is right for the
// Hitachi cores.  The Intel, NEC and MCS-96 parts put the low byte at the
// lower address, so their register blocks derive from this variant instead;
// the byte interface is the same and a device only overrides the widths its
// registers really support as a unit.
#pragma once

#include "common/device.hpp"

namespace emu {

class DeviceLE : public Device {
 public:
  u16 read16(u32 addr) override { return u16(read8(addr) | (u16(read8(addr + 1)) << 8)); }
  void write16(u32 addr, u16 value) override { write8(addr, u8(value)); write8(addr + 1, u8(value >> 8)); }
  u32 read32(u32 addr) override { return u32(read16(addr)) | (u32(read16(addr + 2)) << 16); }
  void write32(u32 addr, u32 value) override { write16(addr, u16(value)); write16(addr + 2, u16(value >> 16)); }
};

}  // namespace emu

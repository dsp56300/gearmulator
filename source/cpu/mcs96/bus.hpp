// MCS-96 address space.
//
// 64 KB, little-endian, on the shared flat bus.  The register file (0000h-
// 00FFh: the zero register, the SFRs and 232 bytes of register RAM) is not on
// the bus: data accesses below 0100h go to the CPU's register file, while
// instruction fetches from those addresses still reach the external bus.  The
// board maps ROM, RAM and its peripherals over the rest.
#pragma once
#include "common/flatbus.hpp"
#include "common/le_device.hpp"
#include "cpu/mcs96/types.hpp"

namespace mcs96 {

using Device = emu::Device;
using CodeSink = emu::CodeSink;
using Bus = emu::FlatBus<16, true>;

constexpr u16 kRegisterFileSize = 0x100;
constexpr u8 kSfrBase = 0x02, kSfrEnd = 0x18;  // SFRs occupy 02h-17h
constexpr u8 kRegisterRamBase = 0x18;

}  // namespace mcs96

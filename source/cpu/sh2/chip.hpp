// Per-device configuration of the SH-1 / SH-2 microcontrollers this core runs.
#pragma once
#include "cpu/sh2/types.hpp"

namespace sh2 {

enum class ChipModel : u8 { SH7014, SH7016, SH7017, SH7034, SH7042 };

struct ChipConfig {
  ChipModel model = ChipModel::SH7014;
  // MD1-MD0: 0 = MCU mode 0 (CS0 8-bit), 1 = MCU mode 1 (CS0 16-bit),
  // 2 = MCU mode 2 (on-chip ROM), 3 = single chip.
  u8 mode = 1;
  bool sh1 = false;            // SH-1 instruction set: the SH-2 additions are illegal
  u32 addr_mask = 0xFFFFFFFFu; // address bits the chip decodes (SH7034: A0-A27)
  u32 rom_size = 0;            // on-chip ROM bytes (0 on the ROMless SH7014)
  u32 ram_size = 0x1000;       // on-chip RAM bytes (4 KB; the top 1 KB is reserved on the 3 KB parts)
  u32 ram_base = 0xFFFFF000u;
  u32 regfield_base = 0xFFFF8000u;
  u32 regfield_size = 0x800;

  bool rom_enabled() const { return mode >= 2 && rom_size != 0; }
  bool cs0_16bit() const { return mode == 1; }
};

inline ChipConfig make_chip_config(ChipModel model, u8 mode) {
  ChipConfig c;
  c.model = model;
  c.mode = mode;
  switch (model) {
    case ChipModel::SH7014: c.rom_size = 0; c.ram_size = 0xC00; break;
    case ChipModel::SH7016: c.rom_size = 0x10000; c.ram_size = 0xC00; break;
    case ChipModel::SH7017: c.rom_size = 0x20000; c.ram_size = 0x1000; break;
    // SH7040-series superset of the SH7014: 256 KB mask ROM (A mask), 4 KB RAM,
    // four DMAC channels, five MTU channels, two A/D units, ports A-F.
    case ChipModel::SH7042: c.rom_size = 0x40000; c.ram_size = 0x1000; break;
    case ChipModel::SH7034:
      // SH-1: A0-A27 decoded (A27 selects the 8-bit or 16-bit shadow of the
      // external areas, table 8.3).  64 KB mask ROM in area 0, registers at
      // H'5FFFE00, and the 4 KB RAM whose shadows fill H'F000000-H'FFFFFFF:
      // the firmware convention is H'FFFF000, so the top 64 KB page of the
      // area is mapped as RAM and mirrored over the rest of it.
      // Modes: 0/1 = ROMless MCU (CS0 8/16-bit), 2 = on-chip ROM.
      c.sh1 = true;
      c.addr_mask = 0x0FFFFFFFu;
      c.rom_size = 0x10000;
      c.ram_size = 0x10000;
      c.ram_base = 0x0FFF0000u;
      c.regfield_base = 0x05FFFE00u;
      c.regfield_size = 0x200;
      break;
  }
  return c;
}

}  // namespace sh2

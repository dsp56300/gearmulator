// Per-device configuration for the H8/500 family members we target.
//
// The CPU core is identical across the family; devices differ in on-chip
// memory, the register field layout, external bus width and peripherals.
#pragma once
#include "cpu/h8500/types.hpp"

namespace h8500 {

class Bus;

enum class ChipModel : u8 { H8_510, H8_570, H8_532 };

struct ChipConfig {
  ChipModel model = ChipModel::H8_510;
  const char* name = "H8/510";

  // MCU operating mode from the MD2-MD0 pins (MDCR.MDS2-0).
  //   H8/510: 1 = expanded min, 8-bit bus   2 = expanded min, 16-bit bus
  //           3 = expanded max, 8-bit bus   4 = expanded max, 16-bit bus
  u8 mode = 2;

  // On-chip memory (page 0).  size 0 = absent.
  u32 rom_base = 0, rom_size = 0;
  u32 ram_base = 0, ram_size = 0;

  // On-chip register field (page 0).  Instruction prefetch from here raises an
  // address error.  8-bit, 3-state access.
  u32 regfield_base = 0xFE80, regfield_size = 0x180;
  // Address range (page 0) whose prefetch raises an address error (register
  // field + external I/O area per H8/510 4.3.1).
  u32 noexec_base = 0xFE80, noexec_size = 0x180;

  // External bus width in this mode.
  bool external_bus_16bit = true;

  // Address lines in maximum mode (24 on the H8/510/570; the H8/532 has 20,
  // so page H'10 aliases page 0).
  u8 max_mode_addr_bits = 24;

  bool max_mode() const;
  unsigned address_bits() const { return max_mode() ? max_mode_addr_bits : 16; }
};

ChipConfig make_chip_config(ChipModel model, u8 mode);

// Apply the chip's fixed memory map to a bus: on-chip ROM/RAM (16-bit, 2-state),
// the no-execute register field, and the default class of external space.
// Peripheral registers are mapped later by the peripheral models themselves.
void configure_bus(Bus& bus, const ChipConfig& cfg);

}  // namespace h8500

#include "cpu/h8500/chip.hpp"

#include "cpu/h8500/bus.hpp"

namespace h8500 {

bool ChipConfig::max_mode() const {
  switch (model) {
    case ChipModel::H8_510:
      return mode == 3 || mode == 4;
    case ChipModel::H8_570:
      // H8/570 (manual 2.2): modes 1/4 = expanded minimum, 3/5/6 = expanded maximum.
      return mode == 3 || mode == 5 || mode == 6;
    case ChipModel::H8_532:
      // H8/532: modes 1,2 = expanded minimum, 3,4 = expanded maximum, 7 = single chip.
      return mode == 3 || mode == 4;
  }
  return false;
}

ChipConfig make_chip_config(ChipModel model, u8 mode) {
  ChipConfig c;
  c.model = model;
  c.mode = mode;
  switch (model) {
    case ChipModel::H8_510:
      // ROM-less, RAM-less member.  Register field H'FE80-H'FFFF (page 0),
      // external I/O area H'FE00-H'FE7F.  16-bit external bus in modes 2/4.
      c.name = "H8/510";
      c.rom_size = 0;
      c.ram_size = 0;
      c.regfield_base = 0xFE80; c.regfield_size = 0x180;
      c.noexec_base = 0xFE80;   c.noexec_size = 0x180;
      c.external_bus_16bit = (mode == 2 || mode == 4);
      break;
    case ChipModel::H8_570:
      // H8/570 hardware manual sections 2 and 14, appendix B: 2-kbyte on-chip
      // RAM at H'F680-H'FE7F, register field H'FE80-H'FF7F (A/D, WDT, ports,
      // SCI, PWM, ISP block, INTC at H'FF40, system registers), no on-chip
      // ROM, 1-Mbyte address space (A19-A16 on port 5) in the maximum modes.
      // 16-bit data bus in modes 1, 3 and 5; 8-bit in modes 4 and 6.
      c.name = "H8/570";
      c.rom_size = 0;
      c.ram_base = 0xF680; c.ram_size = 0x0800;
      c.regfield_base = 0xFE80; c.regfield_size = 0x100;
      c.noexec_base = 0xFE80;   c.noexec_size = 0x100;
      c.external_bus_16bit = (mode == 1 || mode == 3 || mode == 5);
      c.max_mode_addr_bits = 20;
      break;
    case ChipModel::H8_532:
      // 32-kbyte ROM at H'0000, 1-kbyte RAM at H'FB80-H'FF7F (RAMCR.RAME
      // gated), register field H'FF80-H'FFFF, 8-bit external bus, and only
      // 20 address lines (1 MB; page H'10 aliases page 0).
      // TODO(h8532): confirm against the H8/532 hardware manual.
      c.name = "H8/532";
      c.rom_base = 0x0000; c.rom_size = 0x8000;
      c.ram_base = 0xFB80; c.ram_size = 0x0400;
      c.regfield_base = 0xFF80; c.regfield_size = 0x80;
      c.noexec_base = 0xFF80;   c.noexec_size = 0x80;
      c.external_bus_16bit = false;
      c.max_mode_addr_bits = 20;
      break;
  }
  return c;
}

void configure_bus(Bus& bus, const ChipConfig& cfg) {
  // External space defaults to the slow (3-state) class of the chip's bus
  // width; boards map their RAM/ROM/devices over it with the right class.
  bus.set_unmapped(cfg.external_bus_16bit ? BusClass::W16_S3 : BusClass::W8_S3);
  if (cfg.rom_size) bus.map_rom(cfg.rom_base, cfg.rom_size, BusClass::W16_S2);
  if (cfg.ram_size) bus.map_ram(cfg.ram_base, cfg.ram_size, BusClass::W16_S2);
  bus.set_noexec(cfg.noexec_base, cfg.noexec_size);
}

}  // namespace h8500

// I/O ports (H8/510 hardware manual section 9) and the plain system-control
// registers that have no behaviour of their own in the emulator.
//
// Ports 1-8 at H'FE80-H'FE8F:
//   FE80 P1DDR  FE81 P2DDR  FE82 P1DR  FE83 P2DR
//   FE84 P3DDR  FE85 P4DDR  FE86 P3DR  FE87 P4DR
//   FE88 P5DDR  FE89 P6DDR  FE8A P5DR  FE8B P6DR
//   FE8D P8DDR  FE8E P7DR (input only)  FE8F P8DR
// A data-direction register is write-only (reads H'FF).  Reading a data
// register returns the latch for output pins and the pin level for input pins
// (appendix C).  In modes 2/4 port 1 is the data bus and reads all ones; in
// modes 3/4 port 2 is the address bus and reads its latch.  The host supplies
// pin levels with set_pins() and observes outputs through the write hook.
#pragma once
#include <functional>

#include "cpu/h8500/bus.hpp"
#include "common/iomux.hpp"
#include "cpu/h8500/chip.hpp"

namespace h8500 {

class Ports final : public Device {
 public:
  // Called after a data-register write: port number 1..8, latch, direction.
  using WriteHook = std::function<void(unsigned port, u8 dr, u8 ddr)>;
  // Called on a data-register read with the value the port model computed, so
  // a board can substitute the level of a pin it drives itself.
  using ReadHook = std::function<u8(unsigned port, u8 value)>;

  explicit Ports(const ChipConfig& cfg, u32 base = 0xFE80);

  // The H8/532 has a ninth port whose pair of registers lives outside the
  // block (P9DDR / P9DR at H'FFFE / H'FFFF); 0 = the chip has no port 9.
  void set_port9(u32 ddr_addr) { p9_ddr_addr_ = ddr_addr; }

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  // --- host side -----------------------------------------------------------
  void set_pins(unsigned port, u8 levels) { p_[idx(port)].pins = levels; }
  void set_write_hook(WriteHook h) { hook_ = std::move(h); }
  void set_read_hook(ReadHook h) { read_hook_ = std::move(h); }
  // Levels driven on the pins: latch where DDR = 1, host input elsewhere.
  u8 pin_levels(unsigned port) const {
    const P& p = p_[idx(port)];
    return u8((p.dr & p.ddr) | (p.pins & ~p.ddr));
  }
  u8 ddr(unsigned port) const { return p_[idx(port)].ddr; }
  u8 dr(unsigned port) const { return p_[idx(port)].dr; }

 private:
  struct P { u8 ddr = 0, dr = 0, pins = 0xFF; };
  static unsigned idx(unsigned port) { return (port - 1) & 15; }
  u8 read_dr(unsigned port) const;
  const u8* ddr_map() const;
  unsigned input_only_port() const;

  const ChipConfig& cfg_;
  u32 base_;
  u32 p9_ddr_addr_ = 0;
  P p_[9];
  WriteHook hook_;
  ReadHook read_hook_;
};

// Registers whose only observable behaviour is storage with reset values and
// read-only bits: RFSHCR (H'FED8), WCR (H'FF14), ARBT/AR3T (H'FF16/17), MDCR
// (H'FF19), SBYCR (H'FF1A), BRCR (H'FF1B).  Wait-state and bus-area settings
// are recorded but do not yet retime the bus (TODO(bus-controller)).
class SysRegs final : public Device {
 public:
  explicit SysRegs(const ChipConfig& cfg) : cfg_(cfg) { reset(); }
  void map(emu::IoMux& mux);
  void reset();
  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  u8 rfshcr() const { return rfshcr_; }
  u8 wcr() const { return wcr_; }
  u8 arbt() const { return arbt_; }
  u8 ar3t() const { return ar3t_; }
  u8 sbycr() const { return sbycr_; }
  u8 brcr() const { return brcr_; }

 private:
  const ChipConfig& cfg_;
  u8 rfshcr_ = 0xD8, wcr_ = 0xF3, arbt_ = 0xFF, ar3t_ = 0x00, sbycr_ = 0x7F, brcr_ = 0xFE;
};

}  // namespace h8500

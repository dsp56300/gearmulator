// H8/570 I/O ports and system control registers (hardware manual section 9,
// appendix B).
//
// Ports 1, 5, 6, 8-12 have a data register in H'FE8C-H'FE97 and a data
// direction register in H'FF2C-H'FF37; port 7 is input only.  Reading a data
// register returns the latch for output pins and the pin level for input
// pins.  The host supplies pin levels with set_pins() and observes outputs
// through the write hook.
//
// SysRegs570 holds SYSCR8-10 (H'FF23-25, IOF pin selects), WSC (H'FF48),
// RAMCR (H'FF49: RAME, IBE = ISP bus enable), MDCR (H'FF4A, read-only mode
// pins), SBYCR (H'FF4B) and SYSCR1 (H'FF4C: IRQ0E, NMIEG, BRLE), feeding
// IRQ0E / NMIEG to the interrupt controller and IBE to a hook.
#pragma once
#include <functional>

#include "cpu/h8500/bus.hpp"
#include "cpu/h8500/chip.hpp"
#include "cpu/h8500/intc.hpp"
#include "common/iomux.hpp"

namespace h8500 {

class Ports570 final : public Device {
 public:
  using WriteHook = std::function<void(unsigned port, u8 dr, u8 ddr)>;
  static constexpr unsigned kPorts = 13;  // indexed by port number 1..12

  Ports570() { reset(); }

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  void set_pins(unsigned port, u8 levels) { if (port < kPorts) p_[port].pins = levels; }
  void set_write_hook(WriteHook h) { hook_ = std::move(h); }
  u8 pin_levels(unsigned port) const {
    const P& p = p_[port < kPorts ? port : 0];
    return u8((p.dr & p.ddr) | (p.pins & ~p.ddr));
  }
  u8 ddr(unsigned port) const { return p_[port < kPorts ? port : 0].ddr; }
  u8 dr(unsigned port) const { return p_[port < kPorts ? port : 0].dr; }

 private:
  struct P { u8 ddr = 0, dr = 0, pins = 0xFF; };
  P p_[kPorts];
  WriteHook hook_;
};

class SysRegs570 final : public Device {
 public:
  SysRegs570(const ChipConfig& cfg, Intc& intc) : cfg_(cfg), intc_(intc) { reset(); }

  void map(emu::IoMux& mux);
  void reset();
  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  // RAMCR.IBE changed (true = the ISP may master the bus).
  void set_ibe_hook(std::function<void(bool)> hook) { ibe_hook_ = std::move(hook); }
  bool ibe() const { return (ramcr_ & 0x40) != 0; }
  u8 syscr1() const { return syscr1_; }
  u8 ramcr() const { return ramcr_; }

 private:
  const ChipConfig& cfg_;
  Intc& intc_;
  u8 syscr8_ = 0, syscr9_ = 0, syscr10_ = 0;
  u8 wsc_ = 0, ramcr_ = 0xFF, sbycr_ = 0x7F, syscr1_ = 0x87;
  std::function<void(bool)> ibe_hook_;
};

}  // namespace h8500

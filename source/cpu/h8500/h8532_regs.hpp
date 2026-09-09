// H8/532-only register blocks: the three 8-bit PWM timers and the system
// control registers that differ from the H8/510's (H8/532 hardware manual
// appendix B).
//
// The PWM outputs are register models only. The counter is kept because
// firmware occasionally polls TCNT to phase a software task against it, but
// nothing in this tree listens to a PWM pin as a waveform — a board that cares
// reads the duty through duty() and treats it as a DC level.
#pragma once
#include "cpu/h8500/chip.hpp"
#include "cpu/h8500/intc.hpp"
#include <functional>

#include "common/iomux.hpp"

namespace h8500 {

class Bus;

// PWM1-3 at H'FFC0, H'FFC4, H'FFC8: TCR, DTR, TCNT on a stride of four.
class Pwm532 final : public Device {
 public:
  static constexpr unsigned kChannels = 3;

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  u8 duty(unsigned ch) const { return ch < kChannels ? c_[ch].dtr : u8(0); }

 private:
  static constexpr u32 kBase = 0xFFC0;
  struct C { u8 tcr = 0x38, dtr = 0, tcnt = 0; };
  C c_[kChannels];
};

// The plain system-control registers: WCR H'FFF8, RAMCR H'FFF9, MDCR H'FFFA,
// SBYCR H'FFFB and P1CR H'FFFC. P1CR carries the IRQ0 / IRQ1 enables — the
// H8/532 has no IRQCR — so writes to it reach the interrupt controller.
class SysRegs532 final : public Device {
 public:
  SysRegs532(const ChipConfig& cfg, Intc& intc) : cfg_(cfg), intc_(intc) { reset(); }

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  // RAMCR.RAME: with it clear the on-chip RAM addresses fall through to the
  // external bus. The machine re-maps the RAM lines when this changes.
  bool ram_enabled() const { return (ramcr_ & 0x80) != 0; }
  using RameHook = std::function<void(bool enabled)>;
  void set_rame_hook(RameHook h) { rame_hook_ = std::move(h); }

  u8 wcr() const { return wcr_; }
  u8 p1cr() const { return p1cr_; }

 private:
  const ChipConfig& cfg_;
  Intc& intc_;
  u8 wcr_ = 0xF3, ramcr_ = 0xFF, sbycr_ = 0x7F, p1cr_ = 0x80;
  RameHook rame_hook_;
};

}  // namespace h8500

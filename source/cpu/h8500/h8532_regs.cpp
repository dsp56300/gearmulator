#include "cpu/h8500/h8532_regs.hpp"

namespace h8500 {

void Pwm532::map(emu::IoMux& mux) { mux.assign(kBase, 4 * kChannels, this); }

void Pwm532::reset() {
  for (C& c : c_) c = C{};
}

u8 Pwm532::read8(u32 addr) {
  const u32 off = addr - kBase;
  const unsigned ch = off >> 2;
  if (ch >= kChannels) return 0xFF;
  switch (off & 3) {
    case 0: return c_[ch].tcr;
    case 1: return c_[ch].dtr;
    case 2: return c_[ch].tcnt;
    default: return 0xFF;
  }
}

void Pwm532::write8(u32 addr, u8 value) {
  const u32 off = addr - kBase;
  const unsigned ch = off >> 2;
  if (ch >= kChannels) return;
  switch (off & 3) {
    case 0: c_[ch].tcr = value; break;
    case 1: c_[ch].dtr = value; break;
    case 2: c_[ch].tcnt = value; break;
    default: break;
  }
}

void SysRegs532::map(emu::IoMux& mux) { mux.assign(0xFFF8, 5, this); }

void SysRegs532::reset() {
  wcr_ = 0xF3;
  ramcr_ = 0xFF;  // RAME set: the on-chip RAM answers out of reset
  sbycr_ = 0x7F;
  p1cr_ = 0x80;   // IRQ0 / IRQ1 disabled until the program enables them
  intc_.set_irq_enable(0, false);
  intc_.set_irq_enable(1, false);
}

u8 SysRegs532::read8(u32 addr) {
  switch (addr) {
    case 0xFFF8: return wcr_;
    case 0xFFF9: return ramcr_;
    // MDCR reports the mode pins in bits 2-0; the rest read as ones.
    case 0xFFFA: return u8(0xF8 | (cfg_.mode & 7));
    case 0xFFFB: return sbycr_;
    case 0xFFFC: return p1cr_;
    default: return 0xFF;
  }
}

void SysRegs532::write8(u32 addr, u8 value) {
  switch (addr) {
    case 0xFFF8: wcr_ = u8(value | 0xF0); break;
    case 0xFFF9: {
      const bool was = ram_enabled();
      ramcr_ = u8(value | 0x7F);  // only RAME is writable
      if (rame_hook_ && ram_enabled() != was) rame_hook_(ram_enabled());
      break;
    }
    case 0xFFFB: sbycr_ = u8(value | 0x78); break;
    case 0xFFFC:
      p1cr_ = value;
      intc_.set_irq_enable(0, (value & 0x20) != 0);
      intc_.set_irq_enable(1, (value & 0x40) != 0);
      break;
    default: break;  // MDCR is read-only
  }
}

}  // namespace h8500

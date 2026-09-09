#include "cpu/h8500/h8570_regs.hpp"

#include "common/iomux.hpp"

namespace h8500 {

namespace {
// Data register address -> port number (0 = none), for H'FE8C..H'FE97.
constexpr u8 kDrPort[12] = {1, 0, 0, 0, 5, 6, 7, 8, 9, 10, 11, 12};
// Data direction register address -> port number, for H'FF2C..H'FF37.
constexpr u8 kDdrPort[12] = {1, 0, 0, 0, 5, 6, 0, 8, 9, 10, 11, 12};
constexpr u32 kDrBase = 0xFE8C, kDdrBase = 0xFF2C;
}  // namespace

void Ports570::map(emu::IoMux& mux) {
  mux.assign(kDrBase, 12, this);
  mux.assign(kDdrBase, 12, this);
}

void Ports570::reset() {
  for (P& p : p_) { p.ddr = 0; p.dr = 0; }
  p_[1].ddr = 0x01;  // P10DDR = 1
}

u8 Ports570::read8(u32 addr) {
  if (addr >= kDrBase && addr < kDrBase + 12) {
    const unsigned port = kDrPort[addr - kDrBase];
    if (!port) return 0xFF;
    const P& p = p_[port];
    if (port == 7) return p.pins;  // input only
    return u8((p.dr & p.ddr) | (p.pins & ~p.ddr));
  }
  return 0xFF;  // DDRs are write-only
}

void Ports570::write8(u32 addr, u8 v) {
  if (addr >= kDrBase && addr < kDrBase + 12) {
    const unsigned port = kDrPort[addr - kDrBase];
    if (!port || port == 7) return;
    p_[port].dr = v;
    if (hook_) hook_(port, v, p_[port].ddr);
  } else if (addr >= kDdrBase && addr < kDdrBase + 12) {
    const unsigned port = kDdrPort[addr - kDdrBase];
    if (!port) return;
    p_[port].ddr = v;
    if (hook_) hook_(port, p_[port].dr, p_[port].ddr);
  }
}

// ---------------------------------------------------------------------------

void SysRegs570::map(emu::IoMux& mux) {
  mux.assign(0xFF23, 3, this);
  mux.assign(0xFF48, 5, this);
}

void SysRegs570::reset() {
  syscr8_ = syscr9_ = syscr10_ = 0;
  wsc_ = 0;
  ramcr_ = 0xFF;
  sbycr_ = 0x7F;
  syscr1_ = 0x87;
  intc_.set_irq0_enable(false);
  intc_.set_nmi_edge(false);
}

u8 SysRegs570::read8(u32 addr) {
  switch (addr) {
    case 0xFF23: return syscr8_;
    case 0xFF24: return syscr9_;
    case 0xFF25: return syscr10_;
    case 0xFF48: return wsc_;
    case 0xFF49: return u8(ramcr_ | 0x3F);
    case 0xFF4A: return u8(0xC0 | (cfg_.mode & 7));
    case 0xFF4B: return sbycr_;
    case 0xFF4C: return u8(syscr1_ | 0x80);
    default: return 0xFF;
  }
}

void SysRegs570::write8(u32 addr, u8 v) {
  switch (addr) {
    case 0xFF23: syscr8_ = v; return;
    case 0xFF24: syscr9_ = v; return;
    case 0xFF25: syscr10_ = v; return;
    case 0xFF48: wsc_ = v; return;
    case 0xFF49: {
      const bool was = ibe();
      ramcr_ = u8(v | 0x3F);
      if (was != ibe() && ibe_hook_) ibe_hook_(ibe());
      return;
    }
    case 0xFF4B: sbycr_ = v; return;
    case 0xFF4C:
      syscr1_ = u8(v | 0x80);
      intc_.set_irq0_enable((v & 0x20) != 0);
      intc_.set_nmi_edge((v & 0x10) != 0);
      return;
    default: return;
  }
}

}  // namespace h8500

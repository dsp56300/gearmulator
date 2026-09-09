#include "cpu/h8500/intc.hpp"

#include "common/iomux.hpp"

namespace h8500 {

namespace {

constexpr IrqSrcInfo S(u8 vector, u8 ipr, bool high, u8 rank, u8 dte = 0xFF, u8 bit = 0) {
  return IrqSrcInfo{vector, ipr, high, rank, dte, bit};
}
constexpr IrqSrcInfo N() { return IrqSrcInfo{0, 0, false, IrqSrcInfo::kAbsent, 0xFF, 0}; }

// H8/510: Table 5-2 (vectors, IPR bits) and Table 6-3 (DTE bits).  Vector
// numbers are minimum-mode addresses / 2.  Register addresses per appendix B:
// IPRA-IPRD H'FF00-03, DTEA-DTED H'FF08-0B, NMICR H'FF1C, IRQCR H'FF1D.
const IntcLayout kH8510 = {
    0xFF00, 0xFF08, 0xFF1C, 0xFF1D,
    {{
        S(0x40 / 2, 0, true, 0, 0, 4),    // Irq0        IPRA 6-4   DTEA bit 4
        S(0x42 / 2, 0, true, 1),          // Wdt         (handled as IRQ0)
        S(0x48 / 2, 0, false, 2, 0, 0),   // Irq1        IPRA 2-0   DTEA bit 0
        S(0x4A / 2, 0, false, 3, 0, 1),   // Irq2                   DTEA bit 1
        S(0x4C / 2, 0, false, 4, 0, 2),   // Irq3                   DTEA bit 2
        S(0x50 / 2, 1, true, 5, 1, 4),    // Frt1Ici     IPRB 6-4   DTEB bit 4
        S(0x52 / 2, 1, true, 6, 1, 5),    // Frt1Ocia               DTEB bit 5
        S(0x54 / 2, 1, true, 7, 1, 6),    // Frt1Ocib               DTEB bit 6
        S(0x56 / 2, 1, true, 8),          // Frt1Fovi
        S(0x58 / 2, 1, false, 9, 1, 0),   // Frt2Ici     IPRB 2-0   DTEB bit 0
        S(0x5A / 2, 1, false, 10, 1, 1),  // Frt2Ocia               DTEB bit 1
        S(0x5C / 2, 1, false, 11, 1, 2),  // Frt2Ocib               DTEB bit 2
        S(0x5E / 2, 1, false, 12),        // Frt2Fovi
        N(), N(), N(), N(),               // no FRT3 (H8/532 only)
        S(0x60 / 2, 2, true, 13, 2, 4),   // TmrCmia     IPRC 6-4   DTEC bit 4
        S(0x62 / 2, 2, true, 14, 2, 5),   // TmrCmib                DTEC bit 5
        S(0x64 / 2, 2, true, 15),         // TmrOvi
        S(0x68 / 2, 2, false, 16),        // Sci1Eri     IPRC 2-0
        S(0x6A / 2, 2, false, 17, 2, 1),  // Sci1Rxi                DTEC bit 1
        S(0x6C / 2, 2, false, 18, 2, 2),  // Sci1Txi                DTEC bit 2
        S(0x70 / 2, 3, true, 19),         // Sci2Eri     IPRD 6-4
        S(0x72 / 2, 3, true, 20, 3, 5),   // Sci2Rxi                DTED bit 5
        S(0x74 / 2, 3, true, 21, 3, 6),   // Sci2Txi                DTED bit 6
        S(0x78 / 2, 3, false, 22, 3, 0),  // Adi         IPRD 2-0   DTED bit 0
        N(), N(), N(),                    // PWM (H8/570 only)
        N(), N(), N(), N(), N(), N(), N(), N(),  // ISF0-7
        N(), N(), N(), N(), N(), N(), N(), N(),  // ISF8-15
    }},
};

// H8/532: table 5-2.  Vector numbers are the vector-table byte address / 4
// (= minimum-mode address / 2): IRQ0 at H'80 is 32, FRT1's OCIA at H'94 is 37.
// Register addresses per appendix B: IPRA-IPRD H'FFF0-F3, DTEA-DTED H'FFF4-F7.
// There is no NMICR and no IRQCR — the chip has no IRQ2/IRQ3, and IRQ0 / IRQ1
// are enabled by P1CR bits 5 and 6, fed in through set_irq_enable().
// The DTC assignments of this chip are not modelled (TODO(h8532-dtc)).
const IntcLayout kH8532 = {
    0xFFF0, 0xFFF4, 0, 0,
    {{
        S(32, 0, true, 0),          // Irq0        IPRA 6-4
        N(),                        // the watchdog resets the chip, it does not interrupt
        S(33, 0, false, 2),         // Irq1        IPRA 2-0
        N(), N(),                   // no IRQ2 / IRQ3
        S(36, 1, true, 3),          // Frt1Ici     IPRB 6-4
        S(37, 1, true, 4),          // Frt1Ocia
        S(38, 1, true, 5),          // Frt1Ocib
        S(39, 1, true, 6),          // Frt1Fovi
        S(40, 1, false, 7),         // Frt2Ici     IPRB 2-0
        S(41, 1, false, 8),         // Frt2Ocia
        S(42, 1, false, 9),         // Frt2Ocib
        S(43, 1, false, 10),        // Frt2Fovi
        S(44, 2, true, 11),         // Frt3Ici     IPRC 6-4
        S(45, 2, true, 12),         // Frt3Ocia
        S(46, 2, true, 13),         // Frt3Ocib
        S(47, 2, true, 14),         // Frt3Fovi
        S(48, 2, false, 15),        // TmrCmia     IPRC 2-0
        S(49, 2, false, 16),        // TmrCmib
        S(50, 2, false, 17),        // TmrOvi
        S(52, 3, true, 18),         // Sci Eri     IPRD 6-4
        S(53, 3, true, 19),         // Sci Rxi
        S(54, 3, true, 20),         // Sci Txi
        N(), N(), N(),              // one SCI only
        S(56, 3, false, 21),        // Adi         IPRD 2-0
        N(), N(), N(),              // PWM (H8/570 only)
        N(), N(), N(), N(), N(), N(), N(), N(),  // ISF0-7
        N(), N(), N(), N(), N(), N(), N(), N(),  // ISF8-15
    }},
};

// H8/570: Table 5.2 and 5.3.  Vector numbers = maximum-mode address / 4
// (= minimum-mode address / 2).  IPRA-IPRD H'FF40-43, DTEA-DTED H'FF44-47;
// IRQ0E and NMIEG live in SYSCR1 (fed through set_irq0_enable / set_nmi_edge).
// The DTC assignments of this chip are not modelled (TODO(h8570-dtc)).
const IntcLayout kH8570 = {
    0xFF40, 0xFF44, 0, 0,
    {{
        S(0x80 / 4, 0, true, 0),    // Irq0        IPRA 6-4
        S(0x88 / 4, 0, true, 1),    // Wdt         IPRA 6-4
        N(), N(), N(),              // no IRQ1-3
        N(), N(), N(), N(),         // no FRT1
        N(), N(), N(), N(),         // no FRT2
        N(), N(), N(), N(),         // no FRT3
        N(), N(), N(),              // no 8-bit timer
        S(0xE0 / 4, 3, true, 21),   // SCI ERI     IPRD 6-4
        S(0xE4 / 4, 3, true, 22),   // SCI RXI
        S(0xE8 / 4, 3, true, 23),   // SCI TXI
        N(), N(), N(),              // one SCI only
        S(0xF0 / 4, 3, false, 24),  // ADI         IPRD 2-0
        S(0x90 / 4, 0, false, 2),   // PwmOcf0     IPRA 2-0
        S(0x94 / 4, 0, false, 3),   // PwmOcf1
        S(0x98 / 4, 0, false, 4),   // PwmOcf2
        S(0xA0 / 4, 1, false, 5),   // Isf0        IPRB 2-0
        S(0xA4 / 4, 1, false, 6),   // Isf1
        S(0xA8 / 4, 1, false, 7),   // Isf2
        S(0xAC / 4, 1, false, 8),   // Isf3
        S(0xB0 / 4, 1, true, 9),    // Isf4        IPRB 6-4
        S(0xB4 / 4, 1, true, 10),   // Isf5
        S(0xB8 / 4, 1, true, 11),   // Isf6
        S(0xBC / 4, 1, true, 12),   // Isf7
        S(0xC0 / 4, 2, false, 13),  // Isf8        IPRC 2-0
        S(0xC4 / 4, 2, false, 14),  // Isf9
        S(0xC8 / 4, 2, false, 15),  // Isf10
        S(0xCC / 4, 2, false, 16),  // Isf11
        S(0xD0 / 4, 2, true, 17),   // Isf12       IPRC 6-4
        S(0xD4 / 4, 2, true, 18),   // Isf13
        S(0xD8 / 4, 2, true, 19),   // Isf14
        S(0xDC / 4, 2, true, 20),   // Isf15
    }},
};

bool edge_latched(IrqSrc s) { return s == IrqSrc::Irq1 || s == IrqSrc::Irq2 || s == IrqSrc::Irq3; }

}  // namespace

const IntcLayout& h8510_intc_layout() { return kH8510; }
const IntcLayout& h8532_intc_layout() { return kH8532; }
const IntcLayout& h8570_intc_layout() { return kH8570; }
const IntcLayout& intc_layout_for(ChipModel model) {
  switch (model) {
    case ChipModel::H8_570: return kH8570;
    case ChipModel::H8_532: return kH8532;
    case ChipModel::H8_510: break;
  }
  return kH8510;
}

Intc::Intc(Cpu& cpu, const IntcLayout& layout) : cpu_(cpu), lay_(layout) {
  cpu_.set_irq_ack_sink(this);
}

Intc::~Intc() { cpu_.set_irq_ack_sink(nullptr); }

void Intc::map(emu::IoMux& mux) {
  mux.assign(lay_.ipr_base, 4, this);
  mux.assign(lay_.dte_base, 4, this);
  if (lay_.nmicr) mux.assign(lay_.nmicr, 1, this);
  if (lay_.irqcr) mux.assign(lay_.irqcr, 1, this);
}

void Intc::reset() {
  req_.fill(false);
  latched_.fill(false);
  for (u8& v : ipr_) v = 0;
  for (u8& v : dte_) v = 0;
  nmicr_ = 0xFE;
  irqcr_ = 0xF0;
  update();
}

u8 Intc::level_of(IrqSrc src) const {
  const IrqSrcInfo& i = lay_.src[size_t(src)];
  return u8((ipr_[i.ipr] >> (i.ipr_high ? 4 : 0)) & 7);
}

bool Intc::dtc_enabled(IrqSrc src) const {
  const IrqSrcInfo& i = lay_.src[size_t(src)];
  return i.dte != 0xFF && ((dte_[i.dte] >> i.dte_bit) & 1);
}

// Present the highest-priority pending request to the CPU; among equal
// levels the layout's rank (Table 5-2 order) decides.
void Intc::update() {
  unsigned best_level = 0;
  u8 best_rank = 0xFF;
  int best = -1;
  for (size_t s = 0; s < size_t(IrqSrc::Count); ++s) {
    if (!req_[s] && !latched_[s]) continue;
    const IrqSrcInfo& info = lay_.src[s];
    if (info.rank == IrqSrcInfo::kAbsent) continue;
    // A module request routed to the DTC is handed over and no longer
    // competes for the CPU (the DTC clears the module flag itself); a
    // count-exhausted latch always goes to the CPU.
    if (req_[s] && !latched_[s] && dtc_enabled(IrqSrc(s)) && dtc_) {
      if (dtc_->dtc_request(IrqSrc(s), info.vector)) continue;
    }
    const unsigned lvl = level_of(IrqSrc(s));
    if (lvl > best_level || (lvl == best_level && best >= 0 && info.rank < best_rank)) {
      best_level = lvl;
      best_rank = info.rank;
      best = int(s);
    }
  }
  if (best < 0 || best_level == 0) { cpu_.set_irq(0, 0); return; }
  cpu_.set_irq(u8(best_level), lay_.src[size_t(best)].vector);
}

void Intc::raise_cpu_interrupt(IrqSrc src) {
  latched_[size_t(src)] = true;
  update();
}

void Intc::set_request(IrqSrc src, bool active) {
  if (req_[size_t(src)] == active) return;
  req_[size_t(src)] = active;
  update();
}

void Intc::set_irq_pin(unsigned n, bool low) {
  if (n > 3) return;
  const bool was_low = irq_pin_low_[n];
  irq_pin_low_[n] = low;
  if (!irq_enabled(n)) return;
  if (n == 0) {
    set_request(IrqSrc::Irq0, low);  // level-sensed
  } else if (low && !was_low) {
    set_request(IrqSrc(size_t(IrqSrc::Irq1) + n - 1), true);  // falling edge latches
  }
}

void Intc::set_irq0_enable(bool on) { set_irq_enable(0, on); }

void Intc::set_irq_enable(unsigned n, bool on) {
  if (n > 3) return;
  irqcr_ = u8((irqcr_ & ~(1u << n)) | (on ? (1u << n) : 0));
  if (n == 0)
    set_request(IrqSrc::Irq0, on && irq_pin_low_[0]);
  else if (!on)
    set_request(IrqSrc(size_t(IrqSrc::Irq1) + n - 1), false);
}

void Intc::set_nmi_pin(bool high) {
  const bool was_high = nmi_pin_high_;
  nmi_pin_high_ = high;
  const bool rising_edge_selected = nmicr_ & 1;
  const bool edge = rising_edge_selected ? (high && !was_high) : (!high && was_high);
  if (edge) cpu_.request_nmi();
}

u8 Intc::read8(u32 addr) {
  if (addr >= lay_.ipr_base && addr < lay_.ipr_base + 4) return ipr_[addr - lay_.ipr_base];
  if (addr >= lay_.dte_base && addr < lay_.dte_base + 4) return dte_[addr - lay_.dte_base];
  if (lay_.nmicr && addr == lay_.nmicr) return nmicr_;
  if (lay_.irqcr && addr == lay_.irqcr) return irqcr_;
  return 0xFF;
}

void Intc::write8(u32 addr, u8 value) {
  if (addr >= lay_.ipr_base && addr < lay_.ipr_base + 4) {
    // TODO(intc-timing): the new level takes effect two states after the
    // writing instruction; we apply it immediately.
    ipr_[addr - lay_.ipr_base] = value & 0x77;  // bits 7 and 3 read as 0
    update();
  } else if (addr >= lay_.dte_base && addr < lay_.dte_base + 4) {
    dte_[addr - lay_.dte_base] = value;
    update();
  } else if (lay_.nmicr && addr == lay_.nmicr) {
    nmicr_ = u8(0xFE | (value & 1));
  } else if (lay_.irqcr && addr == lay_.irqcr) {
    irqcr_ = u8(0xF0 | (value & 0x0F));
    // Disabling a pin drops its request; enabling IRQ0 while low requests.
    set_request(IrqSrc::Irq0, irq_enabled(0) && irq_pin_low_[0]);
    for (unsigned n = 1; n <= 3; ++n)
      if (!irq_enabled(n)) set_request(IrqSrc(size_t(IrqSrc::Irq1) + n - 1), false);
  }
}

void Intc::irq_acknowledged(u8 vector) {
  for (size_t s = 0; s < size_t(IrqSrc::Count); ++s) {
    if (lay_.src[s].rank == IrqSrcInfo::kAbsent || lay_.src[s].vector != vector) continue;
    // Sources can share a vector (the H8/510's watchdog rides on IRQ0's):
    // clear the one that actually asked, not merely the first that matches.
    if (!req_[s] && !latched_[s]) continue;
    if (edge_latched(IrqSrc(s))) req_[s] = false;  // held until the sequence begins
    latched_[s] = false;
    break;
  }
  update();
}

}  // namespace h8500

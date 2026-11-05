#include "cpu/m6801/hc11periph.hpp"

#include <algorithm>

namespace m6801 {

using namespace hc11;

namespace {
constexpr u64 kNever = emu::Scheduler::kNever;
constexpr u32 kPrescale[4] = {1, 4, 8, 16};  // TMSK2 PR1:PR0
constexpr u32 kScp[4] = {1, 3, 4, 13};       // BAUD SCP1:SCP0
constexpr u32 kSpiDiv[4] = {2, 4, 16, 32};   // SPCR SPR1:SPR0, E cycles per bit
constexpr u32 kAdcCycles = 32;               // one conversion on the E clock (CSEL clear)
constexpr u8 kOcBit[5] = {0x80, 0x40, 0x20, 0x10, 0x08};  // TFLG1 / TMSK1 OC1F-OC5F
constexpr u8 kIcBit[3] = {0x04, 0x02, 0x01};              // IC1F-IC3F

// The vector of each source and the source HPRIO PSEL promotes.
constexpr u16 kVector[15] = {hc11::kVecIrq, hc11::kVecRti, hc11::kVecIc1, hc11::kVecIc2, hc11::kVecIc3,
                             hc11::kVecOc1, hc11::kVecOc2, hc11::kVecOc3, hc11::kVecOc4, hc11::kVecOc5,
                             hc11::kVecTof, hc11::kVecPaov, hc11::kVecPai, hc11::kVecSpi, hc11::kVecSci};
}  // namespace

Hc11Peripherals::Hc11Peripherals(emu::Scheduler& sched, Cpu& cpu, emu::IoMux& io)
    : sched_(sched), cpu_(cpu), io_(io) {
  // Every byte of the block but the port B / C registers, which the expanded
  // mode leaves to the bus.
  for (u16 a = 0; a < kRegSize; ++a) {
    const bool external = a == kPioc || a == kPortc || a == kPortb || a == kPortcl || a == kDdrc;
    io_.assign(kRegBase + a, 1, external ? nullptr : &regs_);
  }
  cpu_.set_intc(this);
}

Hc11Peripherals::~Hc11Peripherals() {
  cpu_.set_intc(nullptr);
  cancel(timer_event_);
  cancel(tx_event_);
  cancel(rx_event_);
  cancel(idle_event_);
  cancel(spi_event_);
}

void Hc11Peripherals::reset() {
  const u64 t = now();
  reset_at_ = t;
  irq_edge_ = false;
  option_ = 0x10;  // DLY
  hprio_ = 0x25;   // expanded mode: MDA, PSEL = 0101 (IRQ)
  init_ = 0x01;
  // ports: PA3-PA6 and the PORTD outputs cleared, DDRA7 and DDRD inputs
  pa_latch_ = pd_latch_ = ddrd_ = 0;
  // timer: the counter from zero at reset, TOCx all ones, the flags and masks clear
  cancel(timer_event_);
  origin_ = t;
  prescale_ = 1;
  synced_ = t + 1;
  toc_.fill(0xFFFF);
  tic_.fill(0);
  oc_inhibit_.fill(0);
  tflg1_ = tmsk1_ = tflg2_ = tmsk2_ = tctl1_ = tctl2_ = oc1m_ = oc1d_ = 0;
  tcnt_lsb_at_ = ~u64(0);
  pactl_ = pacnt_ = 0;
  pa_synced_ = t;
  // SCI
  cancel(tx_event_);
  cancel(rx_event_);
  cancel(idle_event_);
  baud_ &= 0x07;  // SCR2-SCR0 are not affected by reset
  sccr1_ = sccr2_ = 0;
  scsr_ = kTdre | kTc;
  rdr_ = tdr_ = sci_armed_ = 0;
  tx_busy_ = false;
  tx_ready_at_ = t;
  rx_queue_.clear();
  rx_busy_ = idle_armed_ = false;
  rx_line_free_ = t;
  // SPI
  cancel(spi_event_);
  spcr_ = 0x04;  // CPHA
  spsr_ = spi_armed_ = 0;
  spi_busy_ = false;
  // A/D
  adctl_ = 0;
  adc_running_ = false;
  adc_done_ = 0;
  for (unsigned i = 0; i < 3; ++i) port_changed(Port(i));
  update_irq();
}

// ---------------------------------------------------------------------------
// Interrupts

void Hc11Peripherals::set_xirq(bool asserted) {
  xirq_ = asserted;
  update_irq();
}

void Hc11Peripherals::set_irq(bool asserted) {
  if (asserted && !irq_line_) irq_edge_ = true;
  irq_line_ = asserted;
  update_irq();
}

u32 Hc11Peripherals::pending_sources() const {
  u32 m = 0;
  if ((option_ & kIrqe) ? irq_edge_ : irq_line_) m |= 1u << kSrcIrq;
  if (tflg2_ & tmsk2_ & kRtif) m |= 1u << kSrcRti;
  const u8 t1 = tflg1_ & tmsk1_;
  for (unsigned i = 0; i < 3; ++i) if (t1 & kIcBit[i]) m |= 1u << (kSrcIc1 + i);
  for (unsigned i = 0; i < 5; ++i) if (t1 & kOcBit[i]) m |= 1u << (kSrcOc1 + i);
  const u8 t2 = tflg2_ & tmsk2_;
  if (t2 & kTof) m |= 1u << kSrcTof;
  if (t2 & kPaovf) m |= 1u << kSrcPaov;
  if (t2 & kPaif) m |= 1u << kSrcPai;
  if ((spcr_ & kSpie) && (spsr_ & (kSpif | 0x10))) m |= 1u << kSrcSpi;
  if (sci_irq()) m |= 1u << kSrcSci;
  return m;
}

// HPRIO PSEL3-PSEL0 lifts one source above the fixed order (below XIRQ).
u16 Hc11Peripherals::acknowledge() {
  static constexpr Source kPromoted[16] = {kSrcTof, kSrcPaov, kSrcPai, kSrcSpi, kSrcSci, kSrcIrq, kSrcIrq, kSrcRti,
                                           kSrcIc1, kSrcIc2, kSrcIc3, kSrcOc1, kSrcOc2, kSrcOc3, kSrcOc4, kSrcOc5};
  const u64 t = now();
  timer_sync(t);
  sci_sync(t);
  spi_sync(t);
  if (xirq_ && !(cpu_.ccr() & kX)) return hc11::kVecXirq;
  const u32 m = pending_sources();
  unsigned src = kSrcSci;  // a request that went away within the instruction
  const Source promoted = kPromoted[hprio_ & 0x0F];
  if (m & (1u << promoted)) src = promoted;
  else if (m) src = unsigned(__builtin_ctz(m));
  if (src == kSrcIrq) irq_edge_ = false;
  return kVector[src];
}

// ---------------------------------------------------------------------------
// Register block

u8 Hc11Peripherals::reg_read(u8 a) {
  const u64 t = now();
  switch (a) {
    case kPorta: return porta_read();
    case kPortd: {
      u8 ddr;
      const u8 d = portd_effective(ddr);
      return u8(((pins_in(kPortD) & ~ddr) | (d & ddr)) & 0x3F);
    }
    case kDdrd: return ddrd_;
    case kPorte: return pins_in(kPortE);
    case kCforc: return 0;
    case kOc1m: return oc1m_;
    case kOc1d: return oc1d_;
    case kTcnt: {
      // The high byte read buffers the low byte for the next cycle (a double-byte read).
      const u16 v = count_at(t);
      tcnt_lsb_ = u8(v);
      tcnt_lsb_at_ = t + 1;
      return u8(v >> 8);
    }
    case kTcnt + 1: return t == tcnt_lsb_at_ ? tcnt_lsb_ : u8(count_at(t));
    case kTctl1: return tctl1_;
    case kTctl2: return tctl2_;
    case kTmsk1: return tmsk1_;
    case kTflg1: timer_sync(t); return tflg1_;
    case kTmsk2: return tmsk2_;
    case kTflg2: timer_sync(t); pa_sync(t); return tflg2_;
    case kPactl: return pactl_;
    case kPacnt: pa_sync(t); return pacnt_;
    case kSpcr: return spcr_;
    case kSpsr:
      spi_sync(t);
      spi_armed_ |= spsr_ & (kSpif | kWcol);
      return spsr_;
    case kSpdr:
      spi_sync(t);
      if (spi_armed_) {
        spsr_ &= u8(~spi_armed_);
        spi_armed_ = 0;
        update_irq();
      }
      return spdr_;
    case kBaud: return baud_;
    case kSccr1: return sccr1_;
    case kSccr2: return sccr2_;
    case kScsr:
      sci_sync(t);
      sci_armed_ |= scsr_;
      return scsr_;
    case kScdr: {
      sci_sync(t);
      const u8 clear = sci_armed_ & (kRdrf | kIdle | kOr | kNf | kFe);
      if (clear) {
        scsr_ &= u8(~clear);
        sci_armed_ &= u8(~clear);
        update_irq();
      }
      return rdr_;
    }
    case kAdctl: adc_sync(t); return adctl_;
    case kAdr1: case kAdr1 + 1: case kAdr1 + 2: case kAdr1 + 3: adc_sync(t); return adr_[a - kAdr1];
    case kOption: return option_;
    case kHprio: return hprio_;
    case kInit: return init_;
    case kConfig: return 0x0C;  // NOSEC, NOCOP; no ROM, no EEPROM
    default:
      if (a >= kTic1 && a < kTic1 + 6) return u8(tic_[(a - kTic1) >> 1] >> ((a & 1) ? 0 : 8));
      if (a >= kToc1 && a < kToc1 + 10) return u8(toc_[(a - kToc1) >> 1] >> ((a & 1) ? 0 : 8));
      return 0;  // reserved, COPRST, PPROG, TEST1
  }
}

void Hc11Peripherals::reg_write(u8 a, u8 v) {
  const u64 t = now();
  switch (a) {
    case kPorta: {
      const u8 writable = u8((0x78 | ((pactl_ & kDdra7) ? 0x80 : 0)) & ~porta_oc_pins());
      timer_sync(t);
      pa_latch_ = u8((pa_latch_ & ~writable) | (v & writable));
      port_changed(kPortA);
      break;
    }
    case kPortd: pd_latch_ = u8(v & 0x3F); port_changed(kPortD); break;
    case kDdrd: ddrd_ = u8(v & 0x3F); port_changed(kPortD); break;
    case kCforc:
      // Forced compares: the pin actions without the flags, OC1 last so it wins a shared pin.
      timer_sync(t);
      for (unsigned i = 4; i >= 1; --i) if (v & kOcBit[i]) oc_apply(i);
      if (v & kOcBit[0]) oc_apply(0);
      port_changed(kPortA);
      break;
    case kOc1m: timer_sync(t); oc1m_ = u8(v & 0xF8); timer_reschedule(); break;
    case kOc1d: oc1d_ = u8(v & 0xF8); break;
    case kTctl1: timer_sync(t); tctl1_ = v; timer_reschedule(); break;
    case kTctl2: tctl2_ = u8(v & 0x3F); break;
    case kTmsk1: timer_sync(t); tmsk1_ = v; update_irq(); timer_reschedule(); break;
    case kTflg1: timer_sync(t); tflg1_ &= u8(~v); update_irq(); timer_reschedule(); break;
    case kTmsk2:
      timer_sync(t);
      pa_sync(t);
      if (protected_window() && (v & 3) != (tmsk2_ & 3)) {
        // The prescaler changes under the counter, which keeps its value.
        const u16 c = count_at(t);
        prescale_ = kPrescale[v & 3];
        origin_ = t - u64(c) * prescale_;
        tmsk2_ = u8((tmsk2_ & 0xF0) | (v & 3));
      }
      tmsk2_ = u8((v & 0xF0) | (tmsk2_ & 3));
      update_irq();
      timer_reschedule();
      break;
    case kTflg2: timer_sync(t); pa_sync(t); tflg2_ &= u8(~(v & 0xF0)); update_irq(); timer_reschedule(); break;
    case kPactl: {
      timer_sync(t);
      pa_sync(t);
      const u8 old = pactl_;
      pactl_ = u8(v & 0xF3);
      if ((old ^ pactl_) & kDdra7) port_changed(kPortA);
      timer_reschedule();
      break;
    }
    case kPacnt: pa_sync(t); pacnt_ = v; timer_reschedule(); break;
    case kSpcr: spi_sync(t); spcr_ = v; update_irq(); break;
    case kSpdr: {
      spi_sync(t);
      if (spi_armed_) {
        spsr_ &= u8(~spi_armed_);
        spi_armed_ = 0;
      }
      if (spi_busy_) {
        spsr_ |= kWcol;
      } else if ((spcr_ & kSpe) && (spcr_ & kMstr)) {
        spi_out_ = v;
        spi_busy_ = true;
        spi_event_at_ = t + 8 * kSpiDiv[spcr_ & 3];
        spi_event_ = sched_.schedule(spi_event_at_, &on_spi, this);
      } else {
        spdr_ = v;
      }
      update_irq();
      break;
    }
    case kBaud: sci_sync(t); baud_ = u8(v & 0x37); break;
    case kSccr1: sci_sync(t); sccr1_ = u8(v & 0x58); break;
    case kSccr2: {
      sci_sync(t);
      const u8 old = sccr2_;
      sccr2_ = v;
      const u8 rise = u8(~old & sccr2_), fall = u8(old & ~sccr2_);
      if (rise & kTe) {
        // An idle frame (a preamble of ones) before the transmitter takes data.
        cancel(tx_event_);
        tx_busy_ = false;
        tx_ready_at_ = sci_next_edge(t) + frame_cycles();
        sci_tx_schedule(t);
      }
      if (fall & kTe) {
        cancel(tx_event_);
        tx_busy_ = false;
      }
      if (fall & kRe) {
        cancel(rx_event_);
        cancel(idle_event_);
        rx_busy_ = false;
        rx_queue_.clear();
      }
      if ((old ^ sccr2_) & (kTe | kRe)) port_changed(kPortD);
      update_irq();
      break;
    }
    case kScdr: {
      sci_sync(t);
      tdr_ = v;
      // TDRE and TC clear only when SCSR was read with them set; otherwise
      // the byte just overwrites the register and is not sent.
      const u8 clear = sci_armed_ & (kTdre | kTc);
      if (clear) {
        scsr_ &= u8(~clear);
        sci_armed_ &= u8(~clear);
        update_irq();
        sci_tx_schedule(t);
      }
      break;
    }
    case kAdctl:
      // A write starts a conversion sequence and clears CCF.
      adc_sync(t);
      adctl_ = u8(v & 0x3F);
      adc_running_ = true;
      adc_start_ = t;
      adc_done_ = 0;
      break;
    case kOption: {
      const u8 keep = protected_window() ? 0x00 : 0x33;  // IRQE, DLY, CR1:CR0 are time protected
      option_ = u8((option_ & keep) | (v & ~keep & 0xFB));
      update_irq();
      break;
    }
    case kHprio:
      // PSEL is written only while I is set; RBOOT, SMOD, MDA and IRV only in the special modes.
      if (cpu_.ccr() & kI) hprio_ = u8((hprio_ & 0xF0) | (v & 0x0F));
      update_irq();
      break;
    case kInit: if (protected_window()) init_ = v; break;  // the remap itself is not built
    default:
      if (a >= kToc1 && a < kToc1 + 10) {
        const unsigned i = (a - kToc1) >> 1;
        timer_sync(t);
        if (a & 1) {
          toc_[i] = u16((toc_[i] & 0xFF00) | v);
        } else {
          toc_[i] = u16((v << 8) | (toc_[i] & 0xFF));
          // The compare is inhibited during this cycle and the next, so a
          // double-byte store does not match on a half-written value.
          oc_inhibit_[i] = t;
        }
        timer_reschedule();
      }
      break;  // TICx are read only; reserved, COPRST, PPROG, TEST1
  }
}

// ---------------------------------------------------------------------------
// Ports

void Hc11Peripherals::set_port_input(Port p, u8 pins) {
  const u8 old = port_[p].pins;
  port_[p].pins = pins;
  if (p != kPortA || old == pins) return;
  const u64 t = now();
  // PA2-PA0 are the input captures IC1-IC3 (TCTL2 EDGxB:EDGxA: 01 rising, 10 falling, 11 both).
  for (unsigned ic = 0; ic < 3; ++ic) {
    const unsigned bit = 2 - ic;
    const bool was = (old >> bit) & 1, is = (pins >> bit) & 1;
    if (was == is) continue;
    const unsigned edg = (tctl2_ >> (2 * (2 - ic))) & 3;
    if ((is && (edg & 1)) || (!is && (edg & 2))) capture(ic, t);
  }
  if ((old ^ pins) & 0x80) pa_input_changed((old & 0x80) != 0, (pins & 0x80) != 0);
}

void Hc11Peripherals::set_port_write_hook(Port p, PortWriteHook h) { port_[p].write_hook = std::move(h); }

// PA2-PA0 inputs, PA6-PA3 outputs, PA7 either way (PACTL DDRA7).
u8 Hc11Peripherals::porta_read() const {
  const u8 in = pins_in(kPortA);
  const u8 ddr = port_ddr(kPortA);
  return u8((in & ~ddr) | (pa_latch_ & ddr));
}

u8 Hc11Peripherals::porta_oc_pins() const {
  u8 m = 0;
  for (unsigned i = 1; i < 5; ++i) if (oc_pin_action(i)) m |= u8(1u << (7 - i));
  return m;
}

// Port D as the pins see it: PD1 is TxD (a mark between frames) while TE
// is set, PD0 RxD while RE is.
u8 Hc11Peripherals::portd_effective(u8& ddr) const {
  u8 d = pd_latch_;
  ddr = ddrd_;
  if (sccr2_ & kRe) ddr &= u8(~0x01);
  if (sccr2_ & kTe) { ddr |= 0x02; d |= 0x02; }
  return d;
}

u8 Hc11Peripherals::port_ddr(Port p) const {
  switch (p) {
    case kPortA: return u8(0x78 | ((pactl_ & kDdra7) ? 0x80 : 0));
    case kPortD: { u8 ddr; portd_effective(ddr); return ddr; }
    default: return 0;
  }
}

u8 Hc11Peripherals::port_output(Port p) const {
  u8 d = 0xFF;
  switch (p) {
    case kPortA: d = pa_latch_; break;
    case kPortD: { u8 ddr; d = portd_effective(ddr); break; }
    default: break;
  }
  const u8 ddr = port_ddr(p);
  return u8((d & ddr) | ~ddr);
}

void Hc11Peripherals::port_changed(Port p) {
  PortState& s = port_[p];
  const u8 ddr = port_ddr(p);
  const u8 d = u8(port_output(p) & ddr);
  if (d == s.out && ddr == s.out_ddr) return;
  s.out = d;
  s.out_ddr = ddr;
  if (s.write_hook) s.write_hook(d, ddr);
}

// ---------------------------------------------------------------------------
// Timer

u64 Hc11Peripherals::tick_with(u64 from, u16 value) const {
  // Modular: a prescaler change may put the origin before the machine's time zero.
  const u64 rel = from - origin_;
  const u64 k0 = (rel + prescale_ - 1) / prescale_;
  const u64 k = k0 + u16(value - u16(k0));
  return origin_ + k * prescale_;
}

u64 Hc11Peripherals::oc_match(unsigned i, u64 from) const {
  u64 m = tick_with(from, toc_[i]);
  if (m == oc_inhibit_[i] || m == oc_inhibit_[i] + 1) m = tick_with(oc_inhibit_[i] + 2, toc_[i]);
  return m;
}

// RTIF every 2^(13 + RTR) E cycles, counted from reset.
u64 Hc11Peripherals::next_rti(u64 from) const {
  const u64 p = rti_period();
  const u64 rel = from > reset_at_ ? from - reset_at_ : 0;
  return reset_at_ + std::max<u64>(1, (rel + p - 1) / p) * p;
}

// OC1 drives the PA7-PA3 pins its mask selects; OC2-OC5 drive PA6-PA3 per TCTL1.
bool Hc11Peripherals::oc_pin_action(unsigned i) const {
  if (i == 0) return (oc1m_ & ((pactl_ & kDdra7) ? 0xF8 : 0x78)) != 0;
  return ((tctl1_ >> (2 * (4 - i))) & 3) != 0;
}

void Hc11Peripherals::oc_apply(unsigned i) {
  if (i == 0) {
    const u8 m = u8(oc1m_ & ((pactl_ & kDdra7) ? 0xF8 : 0x78));
    pa_latch_ = u8((pa_latch_ & ~m) | (oc1d_ & m));
    return;
  }
  const u8 bit = u8(1u << (7 - i));
  switch ((tctl1_ >> (2 * (4 - i))) & 3) {
    case 1: pa_latch_ ^= bit; break;
    case 2: pa_latch_ &= u8(~bit); break;
    case 3: pa_latch_ |= bit; break;
    default: break;
  }
}

// Apply the matches of the cycles before `t`, in time order. A flag is
// sticky, so a source with no pin action needs its first match only; one
// with a pin action has an event at every match, so a span holds at most
// one of those.
void Hc11Peripherals::timer_sync(u64 t) {
  if (t <= synced_) return;
  const u8 f1 = tflg1_, f2 = tflg2_, pa = pa_latch_;
  for (;;) {
    std::array<u64, 7> at;
    at.fill(kNever);
    for (unsigned i = 0; i < 5; ++i)
      if (!(tflg1_ & kOcBit[i]) || oc_pin_action(i)) at[i] = oc_match(i, synced_);
    if (!(tflg2_ & kTof)) at[5] = tick_with(synced_, 0);
    if (!(tflg2_ & kRtif)) at[6] = next_rti(synced_);
    const u64 first = *std::min_element(at.begin(), at.end());
    if (first >= t) break;
    // OC5 to OC2 before OC1: OC1's action wins a pin they share in one cycle.
    for (unsigned i = 4; i >= 1; --i)
      if (at[i] == first) { tflg1_ |= kOcBit[i]; oc_apply(i); }
    if (at[0] == first) { tflg1_ |= kOcBit[0]; oc_apply(0); }
    if (at[5] == first) tflg2_ |= kTof;
    if (at[6] == first) tflg2_ |= kRtif;
    synced_ = first + 1;
  }
  synced_ = t;
  if (pa_latch_ != pa) port_changed(kPortA);
  if (tflg1_ != f1 || tflg2_ != f2) update_irq();
}

// The next transition anyone can see: a flag whose interrupt is enabled, a
// compare pin action, the gated pulse accumulator overflowing.
void Hc11Peripherals::timer_reschedule() {
  cancel(timer_event_);
  u64 when = kNever;
  for (unsigned i = 0; i < 5; ++i)
    if (oc_pin_action(i) || (!(tflg1_ & kOcBit[i]) && (tmsk1_ & kOcBit[i]))) when = std::min(when, oc_match(i, synced_) + 1);
  if (!(tflg2_ & kTof) && (tmsk2_ & kTof)) when = std::min(when, tick_with(synced_, 0) + 1);
  if (!(tflg2_ & kRtif) && (tmsk2_ & kRtif)) when = std::min(when, next_rti(synced_) + 1);
  if (!(tflg2_ & kPaovf) && (tmsk2_ & kPaovf)) when = std::min(when, pa_overflow_at());
  if (when != kNever) timer_event_ = sched_.schedule(when, &on_timer, this);
}

void Hc11Peripherals::on_timer(void* self, u64 when, u64) {
  auto* p = static_cast<Hc11Peripherals*>(self);
  p->timer_event_ = 0;
  p->timer_sync(when);
  p->pa_sync(when);
  p->update_irq();
  p->timer_reschedule();
}

void Hc11Peripherals::capture(unsigned ic, u64 t) {
  timer_sync(t);
  tic_[ic] = count_at(t);
  tflg1_ |= kIcBit[ic];
  update_irq();
}

// ---------------------------------------------------------------------------
// Pulse accumulator: PA7 edges (event mode) or E/64 while PA7 is at the
// enabling level (gated mode).

bool Hc11Peripherals::pa_gate_open() const {
  return (pactl_ & kPaen) && (pactl_ & kPamod) && pai_ == !(pactl_ & kPedge);
}

void Hc11Peripherals::pa_count(unsigned n) {
  if (!n) return;
  if (pacnt_ + n > 0xFF) tflg2_ |= kPaovf;
  pacnt_ = u8(pacnt_ + n);
}

// E/64 ticks run from reset.
void Hc11Peripherals::pa_sync(u64 t) {
  if (t <= pa_synced_) return;
  if (pa_gate_open()) {
    const u64 a = (pa_synced_ - reset_at_ + 63) / 64, b = (t - reset_at_ + 63) / 64;
    const u8 f = tflg2_;
    pa_count(unsigned(std::min<u64>(b - a, 0x100)));
    if (tflg2_ != f) update_irq();
  }
  pa_synced_ = t;
}

u64 Hc11Peripherals::pa_overflow_at() const {
  if (!pa_gate_open()) return kNever;
  const u64 next_tick = (pa_synced_ - reset_at_ + 63) / 64;
  return reset_at_ + (next_tick + (0xFF - pacnt_)) * 64 + 1;
}

void Hc11Peripherals::pa_input_changed(bool old_level, bool level) {
  const u64 t = now();
  pa_sync(t);
  pai_ = level;
  (void)old_level;
  if (pactl_ & kPaen) {
    const bool rising = level, wanted = (pactl_ & kPedge) != 0;
    if (!(pactl_ & kPamod)) {
      // Event counting: the selected edge counts and sets PAIF.
      if (rising == wanted) {
        pa_count(1);
        tflg2_ |= kPaif;
      }
    } else if (rising == wanted) {
      // Gated: the trailing edge of the enabling level sets PAIF.
      tflg2_ |= kPaif;
    }
    update_irq();
  }
  timer_reschedule();
}

// ---------------------------------------------------------------------------
// Serial communication interface

u32 Hc11Peripherals::sci_bit_cycles() const { return 16 * kScp[(baud_ >> 4) & 3] * (1u << (baud_ & 7)); }

// The baud generator runs from reset.
u64 Hc11Peripherals::sci_next_edge(u64 t) const {
  const u64 bit = sci_bit_cycles();
  return reset_at_ + ((t - reset_at_) / bit + 1) * bit;
}

bool Hc11Peripherals::sci_irq() const {
  return ((sccr2_ & kTie) && (scsr_ & kTdre)) || ((sccr2_ & kTcie) && (scsr_ & kTc)) ||
         ((sccr2_ & kRie) && (scsr_ & (kRdrf | kOr))) || ((sccr2_ & kIlie) && (scsr_ & kIdle));
}

// Run the SCI transitions due by `t` (a register access inside an
// instruction that started before them).
void Hc11Peripherals::sci_sync(u64 t) {
  for (;;) {
    u64 at = kNever;
    int which = -1;
    if (tx_event_ && tx_event_at_ <= t) { at = tx_event_at_; which = 0; }
    if (rx_event_ && rx_event_at_ <= t && rx_event_at_ < at) { at = rx_event_at_; which = 1; }
    if (idle_event_ && idle_event_at_ <= t && idle_event_at_ < at) { at = idle_event_at_; which = 2; }
    if (which < 0) return;
    if (which == 0) { cancel(tx_event_); sci_tx_event(at); }
    else if (which == 1) { cancel(rx_event_); sci_rx_event(at); }
    else { cancel(idle_event_); scsr_ |= kIdle; idle_armed_ = false; update_irq(); }
  }
}

// SCDR holds a byte and the transmitter is free: it is loaded at the next
// bit edge, not before the preamble or the previous frame ends.
void Hc11Peripherals::sci_tx_schedule(u64 t) {
  if (tx_event_ || tx_busy_ || !(sccr2_ & kTe) || (scsr_ & kTdre)) return;
  tx_event_at_ = std::max(tx_ready_at_, sci_next_edge(t));
  tx_event_ = sched_.schedule(tx_event_at_, &on_tx, this);
}

void Hc11Peripherals::sci_tx_event(u64 when) {
  if (tx_busy_) {
    tx_busy_ = false;
    tx_ready_at_ = when;
    if (tx_hook_) tx_hook_(tx_shift_);
  }
  if (!(scsr_ & kTdre) && (sccr2_ & kTe)) {
    // Back to back: the next byte moves to the shifter, TDRE sets.
    tx_shift_ = tdr_;
    scsr_ = u8((scsr_ | kTdre) & ~kTc);
    tx_busy_ = true;
    tx_event_at_ = when + frame_cycles();
    tx_event_ = sched_.schedule(tx_event_at_, &on_tx, this);
  } else {
    scsr_ |= kTc;  // the line is idle
  }
  update_irq();
}

void Hc11Peripherals::on_tx(void* self, u64 when, u64) {
  auto* p = static_cast<Hc11Peripherals*>(self);
  p->tx_event_ = 0;
  p->sci_tx_event(when);
}

void Hc11Peripherals::receive(const u8* data, size_t len) {
  const u64 t = now();
  if (!(sccr2_ & kRe)) return;
  sci_sync(t);
  rx_queue_.insert(rx_queue_.end(), data, data + len);
  sci_rx_start(t);
}

void Hc11Peripherals::sci_rx_start(u64 t) {
  if (rx_busy_ || rx_queue_.empty()) return;
  rx_shift_ = rx_queue_.front();
  rx_queue_.pop_front();
  rx_busy_ = true;
  rx_start_ = std::max(t, rx_line_free_);
  cancel(idle_event_);
  // The stop bit is sampled in its middle.
  rx_event_at_ = rx_start_ + frame_cycles() - sci_bit_cycles() / 2;
  rx_event_ = sched_.schedule(rx_event_at_, &on_rx, this);
}

void Hc11Peripherals::sci_rx_event(u64 when) {
  rx_busy_ = false;
  rx_line_free_ = rx_start_ + frame_cycles();
  if (scsr_ & kRdrf) {
    scsr_ |= kOr;  // overrun: the byte is lost, SCDR keeps the old one
  } else {
    rdr_ = rx_shift_;
    scsr_ |= kRdrf;
  }
  idle_armed_ = true;
  update_irq();
  if (!rx_queue_.empty()) sci_rx_start(rx_line_free_);
  else sci_idle_schedule();
  (void)when;
}

// IDLE: a full frame of ones after a received character.
void Hc11Peripherals::sci_idle_schedule() {
  cancel(idle_event_);
  if (!idle_armed_) return;
  idle_event_at_ = rx_line_free_ + frame_cycles();
  idle_event_ = sched_.schedule(idle_event_at_, &on_idle, this);
}

void Hc11Peripherals::on_rx(void* self, u64 when, u64) {
  auto* p = static_cast<Hc11Peripherals*>(self);
  p->rx_event_ = 0;
  p->sci_rx_event(when);
}

void Hc11Peripherals::on_idle(void* self, u64, u64) {
  auto* p = static_cast<Hc11Peripherals*>(self);
  p->idle_event_ = 0;
  p->scsr_ |= kIdle;
  p->idle_armed_ = false;
  p->update_irq();
}

// ---------------------------------------------------------------------------
// SPI (master)

void Hc11Peripherals::spi_sync(u64 t) {
  if (spi_event_ && spi_event_at_ <= t) {
    cancel(spi_event_);
    on_spi(this, spi_event_at_, t);
  }
}

void Hc11Peripherals::on_spi(void* self, u64, u64) {
  auto* p = static_cast<Hc11Peripherals*>(self);
  p->spi_event_ = 0;
  p->spi_busy_ = false;
  p->spdr_ = p->spi_hook_ ? p->spi_hook_(p->spi_out_) : u8(0xFF);
  p->spsr_ |= kSpif;
  p->update_irq();
}

// ---------------------------------------------------------------------------
// A/D: four conversions of 32 E cycles each into ADR1-ADR4, of one channel
// or (MULT) of the group CD:CC selects; SCAN repeats the sequence.

unsigned Hc11Peripherals::adc_channel(unsigned k) const {
  return (adctl_ & 0x10) ? unsigned((adctl_ & 0x0C) | k) : unsigned(adctl_ & 0x0F);
}

void Hc11Peripherals::adc_sync(u64 t) {
  if (!adc_running_) return;
  const u64 n = (t - adc_start_) / kAdcCycles;  // conversions finished
  const bool scan = (adctl_ & 0x20) != 0;
  const u64 upto = scan ? n : std::min<u64>(n, 4);
  // Older conversions of a scan were overwritten since; only the last four matter.
  for (u64 j = upto > 4 + adc_done_ ? upto - 4 : adc_done_; j < upto; ++j) {
    const unsigned ch = adc_channel(unsigned(j & 3));
    // Channels 8-11 are reserved; 12 VRH, 13 VRL, 14 (VRH)/2, 15 reserved.
    u8 v = 0;
    if (ch < 8) v = analog_[ch];
    else if (ch == 12) v = 0xFF;
    else if (ch == 14) v = 0x80;
    adr_[j & 3] = v;
  }
  adc_done_ = unsigned(std::min<u64>(upto, 0xFFFFFFFF));
  if (n >= 4) adctl_ |= 0x80;  // CCF
  if (!scan && n >= 4) adc_running_ = false;
}

}  // namespace m6801

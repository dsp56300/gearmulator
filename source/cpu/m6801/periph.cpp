#include "cpu/m6801/periph.hpp"

#include <algorithm>

namespace m6801 {

namespace {
constexpr u64 kNever = emu::Scheduler::kNever;
constexpr u32 kRates[4] = {16, 128, 1024, 4096};  // E cycles per bit, RMCR SS1:SS0
}  // namespace

Peripherals::Peripherals(emu::Scheduler& sched, Cpu& cpu, emu::IoMux& io, ChipModel model, int mode)
    : sched_(sched), cpu_(cpu), io_(io), model_(model), family_(family_of(model)) {
  // TOF: the MC6801 sets it while the counter holds $FFFF, the HD6301 when it reaches $0000.
  tof_value_ = family_ == Family::HD6301 ? 0x0000 : 0xFFFF;
  set_mode(mode);
  cpu_.set_intc(this);
}

Peripherals::~Peripherals() {
  cpu_.set_intc(nullptr);
  cancel(timer_event_);
  cancel(tx_event_);
  cancel(rx_event_);
  cancel(wu_event_);
}

void Peripherals::set_mode(int mode) {
  mode_ = mode;
  const u32 external = external_registers(family_, mode);
  for (u16 a = 0; a < kRegFieldSize; ++a) io_.assign(a, 1, ((external >> a) & 1) ? nullptr : &regs_);
}

void Peripherals::reset() {
  const u64 t = now();
  nmi_ff_ = irq1_ff_ = irq2_ff_ = false;
  for (auto& p : port_) p.ddr = 0;
  // timer: the counter cleared, OCR all ones, the flags and the output level register clear
  cancel(timer_event_);
  frc_origin_ = synced_ = frc_loaded_at_ = t;
  ocf_inhibit_lo_ = 1;
  ocf_inhibit_hi_ = 0;
  ocr_ = 0xFFFF;
  icr_ = 0;
  tcsr_ = tcsr_armed_ = 0;
  frc_lsb_ = frc_msb_ = 0;
  olr_ = false;
  // SCI
  cancel(tx_event_);
  cancel(rx_event_);
  cancel(wu_event_);
  rmcr_ = 0;
  trcsr_ = kTdre;
  rdr_ = tdr_ = sci_armed_ = 0;
  tx_busy_ = false;
  tx_ready_at_ = t;
  rx_queue_.clear();
  rx_busy_ = false;
  rx_line_free_ = rx_idle_since_ = ext_origin_ = t;
  // RAM control: RAME set with standby power present; STBY PWR is not affected.
  const bool was = ram_enabled();
  ramc_ |= kRame;
  const Port& p2 = port_[1];
  p20_ = (p2.pins & 1) != 0;
  for (unsigned i = 0; i < 4; ++i) port_changed(i);
  update_irq2();
  if (!was && ram_hook_) ram_hook_(true);
}

// ---------------------------------------------------------------------------
// Interrupts

void Peripherals::set_nmi(bool asserted) {
  if (asserted && !nmi_line_) nmi_ff_ = true;
  nmi_line_ = asserted;
  cpu_.reeval_irq();
}

void Peripherals::set_irq1(bool asserted) {
  irq1_line_ = asserted;
  if (asserted && !(cpu_.ccr() & kI)) irq1_ff_ = true;
  cpu_.reeval_irq();
}

bool Peripherals::irq2_line() const {
  return ((tcsr_ & kIcf) && (tcsr_ & kEici)) || ((tcsr_ & kOcf) && (tcsr_ & kEoci)) ||
         ((tcsr_ & kTof) && (tcsr_ & kEtoi)) || sci_irq();
}

// A request seen while I is clear sets its flip-flop, which stays set when
// the request goes away (a flag cleared within the same instruction).
void Peripherals::update_irq2() {
  irq2_level_ = irq2_line();
  if (irq2_level_ && !(cpu_.ccr() & kI)) irq2_ff_ = true;
  cpu_.reeval_irq();
}

// MC6801 reference manual 5.3.1: the priority encoder sees the NMI and IRQ1
// flip-flops and the timer sources; an IRQ2 with none of those is taken as
// the SCI's.
u16 Peripherals::acknowledge() {
  timer_sync(now());
  if (nmi_ff_) {
    nmi_ff_ = false;
    return kVecNmi;
  }
  if (irq1_ff_ || irq1_line_) return kVecIrq1;
  if ((tcsr_ & kIcf) && (tcsr_ & kEici)) return kVecIcf;
  if ((tcsr_ & kOcf) && (tcsr_ & kEoci)) return kVecOcf;
  if ((tcsr_ & kTof) && (tcsr_ & kEtoi)) return kVecTof;
  return kVecSci;
}

// ---------------------------------------------------------------------------
// Register field

u8 Peripherals::reg_read(u8 a) {
  const u64 t = now();
  switch (a) {
    case kP1Data: return port_read(0);
    case kP2Data: return port_read(1);
    case kP3Data: return port_read(2);
    case kP4Data: return port_read(3);
    case kTcsr:
      timer_sync(t);
      tcsr_armed_ |= tcsr_ & (kIcf | kOcf | kTof);
      return tcsr_;
    case kFrcH: {
      timer_sync(t);
      const u16 v = frc_at(t);
      frc_lsb_ = u8(v);
      if (tcsr_armed_ & kTof) {
        tcsr_ &= u8(~kTof);
        tcsr_armed_ &= u8(~kTof);
        update_irq2();
        timer_reschedule();
      }
      return u8(v >> 8);
    }
    case kFrcL: return frc_lsb_;
    case kOcrH: return u8(ocr_ >> 8);
    case kOcrL: return u8(ocr_);
    case kIcrH:
      timer_sync(t);
      if (tcsr_armed_ & kIcf) {
        tcsr_ &= u8(~kIcf);
        tcsr_armed_ &= u8(~kIcf);
        update_irq2();
      }
      return u8(icr_ >> 8);
    case kIcrL: return u8(icr_);
    case kTrcsr:
      sci_sync(t);
      sci_armed_ |= trcsr_ & (kRdrf | kOrfe | kTdre);
      return trcsr_;
    case kRdr: {
      sci_sync(t);
      const u8 clear = sci_armed_ & (kRdrf | kOrfe);
      if (clear) {
        trcsr_ &= u8(~clear);
        sci_armed_ &= u8(~clear);
        update_irq2();
      }
      return rdr_;
    }
    case kRamCtrl: return ram_control();
    default: return 0xFF;  // the write-only registers (DDRs, RMCR, TDR) and the reserved bytes
  }
}

void Peripherals::reg_write(u8 a, u8 v) {
  const u64 t = now();
  switch (a) {
    case kP1Ddr: port_[0].ddr = v; port_changed(0); break;
    case kP3Ddr: port_[2].ddr = v; port_changed(2); break;
    case kP4Ddr: port_[3].ddr = v; port_changed(3); break;
    case kP2Ddr: {
      // MC6801: the SCI holds the directions of the pins it uses.
      u8 d = u8(v & 0x1F);
      if (family_ == Family::M6801) {
        if (trcsr_ & kTe) d |= 0x10;
        if (trcsr_ & kRe) d &= u8(~0x08);
        if (rmcr_ & 0x08) d = u8((d & ~0x04) | ((rmcr_ & 0x04) ? 0 : 0x04));
      }
      timer_sync(t);
      port_[1].ddr = d;
      port_changed(1);
      timer_reschedule();  // P21 may have become the compare output
      break;
    }
    case kP1Data: port_[0].data = v; port_changed(0); break;
    case kP2Data: port_[1].data = v; port_changed(1); break;
    case kP3Data: port_[2].data = v; port_changed(2); break;
    case kP4Data: port_[3].data = v; port_changed(3); break;
    case kTcsr:
      timer_sync(t);
      tcsr_ = u8((tcsr_ & (kIcf | kOcf | kTof)) | (v & 0x1F));
      update_irq2();
      timer_reschedule();
      break;
    case kFrcH:
      // Any write to the high byte presets the counter to $FFF8; the HD6301
      // also keeps the byte for a following write of the low byte.
      frc_msb_ = v;
      counter_written(t, 0xFFF8);
      break;
    case kFrcL:
      if (family_ == Family::HD6301) counter_written(t, u16((frc_msb_ << 8) | v));
      break;
    case kOcrH: case kOcrL:
      timer_sync(t);
      if (a == kOcrH) {
        ocr_ = u16((v << 8) | (ocr_ & 0xFF));
        // The compare is inhibited during this cycle and the next, so a
        // double-byte store does not match on a half-written value.
        ocf_inhibit_lo_ = t;
        ocf_inhibit_hi_ = t + 1;
      } else {
        ocr_ = u16((ocr_ & 0xFF00) | v);
      }
      if (tcsr_armed_ & kOcf) {
        tcsr_ &= u8(~kOcf);
        tcsr_armed_ &= u8(~kOcf);
      }
      update_irq2();
      timer_reschedule();
      break;
    case kRmcr: {
      sci_sync(t);
      const u8 old = rmcr_;
      rmcr_ = u8(v & 0x0F);
      if ((old ^ rmcr_) & 0x0C) {
        ext_origin_ = t;
        if (family_ == Family::M6801 && (rmcr_ & 0x08))
          port_[1].ddr = u8((port_[1].ddr & ~0x04) | ((rmcr_ & 0x04) ? 0 : 0x04));
        port_changed(1);
      }
      sci_tx_schedule(t);
      break;
    }
    case kTrcsr: {
      sci_sync(t);
      const u8 old = trcsr_;
      trcsr_ = u8((old & (kRdrf | kOrfe | kTdre)) | (v & 0x1F));
      const u8 rise = u8(~old & trcsr_), fall = u8(old & ~trcsr_);
      if (rise & kTe) {
        // A preamble of ones (nine on the MC6801, ten on the HD6301) before
        // the transmitter takes data.
        if (family_ == Family::M6801) port_[1].ddr |= 0x10;
        cancel(tx_event_);
        tx_busy_ = false;
        tx_ready_at_ = sci_next_edge(t) + sci_halfbits(family_ == Family::HD6301 ? 20 : 18);
        sci_tx_schedule(t);
      }
      if (fall & kTe) {
        cancel(tx_event_);
        tx_busy_ = false;
      }
      if ((rise & kRe) && family_ == Family::M6801) port_[1].ddr &= u8(~0x08);
      if (fall & kRe) {
        cancel(rx_event_);
        cancel(wu_event_);
        rx_busy_ = false;
        rx_queue_.clear();
      }
      if (rise & kWu) sci_wu_schedule(t);
      if (fall & kWu) cancel(wu_event_);
      if ((old ^ trcsr_) & (kTe | kRe)) port_changed(1);
      update_irq2();
      break;
    }
    case kTdr:
      sci_sync(t);
      tdr_ = v;
      // TDRE clears only when TRCSR was read with it set; otherwise the byte
      // just overwrites the register and is not sent.
      if (sci_armed_ & kTdre) {
        trcsr_ &= u8(~kTdre);
        sci_armed_ &= u8(~kTdre);
        update_irq2();
        sci_tx_schedule(t);
      }
      break;
    case kRamCtrl: {
      const bool was = ram_enabled();
      ramc_ = u8(v & (kRame | kStbyPwr));
      if (was != ram_enabled() && ram_hook_) ram_hook_(ram_enabled());
      break;
    }
    default: break;
  }
}

// ---------------------------------------------------------------------------
// Ports

void Peripherals::set_port_input(unsigned port, u8 pins) {
  if (port - 1 >= 4) return;
  port_[port - 1].pins = pins;
  if (port == 2) p20_check();
}

void Peripherals::set_port_write_hook(unsigned port, PortWriteHook h) {
  if (port - 1 >= 4) return;
  port_[port - 1].write_hook = std::move(h);
}

// Port 2 as the pins see it: P21 shows the timer's output level register
// (the data register bit never drives it), P22-P24 belong to the SCI when
// its clock option, receiver and transmitter claim them.
void Peripherals::port2_effective(u8& data, u8& ddr) const {
  u8 d = u8((port_[1].data & ~0x02) | (olr_ ? 0x02 : 0)), dd = u8(port_[1].ddr & 0x1F);
  if (rmcr_ & 0x08) {
    if (rmcr_ & 0x04) dd &= u8(~0x04);  // external clock in
    else { dd |= 0x04; d |= 0x04; }     // bit-rate clock out
  }
  if (trcsr_ & kRe) dd &= u8(~0x08);
  if (trcsr_ & kTe) { dd |= 0x10; d |= 0x10; }  // a mark between frames
  data = u8(d & 0x1F);
  ddr = dd;
}

u8 Peripherals::port_read(unsigned i) const {
  if (i == 1) {
    u8 d, dd;
    port2_effective(d, dd);
    // Bits 7-5: the mode latched from P22-P20 at reset.
    return u8((((pins_in(1) & ~dd) | (d & dd)) & 0x1F) | ((mode_ & 7) << 5));
  }
  const Port& p = port_[i];
  return u8((pins_in(i) & ~p.ddr) | (p.data & p.ddr));
}

u8 Peripherals::port_ddr(unsigned port) const {
  if (port - 1 >= 4) return 0;
  if (port == 2) {
    u8 d, dd;
    port2_effective(d, dd);
    return dd;
  }
  return port_[port - 1].ddr;
}

u8 Peripherals::port_output(unsigned port) const {
  if (port - 1 >= 4) return 0xFF;
  u8 d = port_[port - 1].data, dd = port_[port - 1].ddr;
  if (port == 2) port2_effective(d, dd);
  return u8((d & dd) | ~dd);
}

void Peripherals::port_changed(unsigned i) {
  Port& p = port_[i];
  u8 d = p.data, dd = p.ddr;
  if (i == 1) {
    port2_effective(d, dd);
    p20_check();
  }
  if (d == p.out && dd == p.out_ddr) return;
  p.out = d;
  p.out_ddr = dd;
  if (p.write_hook) p.write_hook(d, dd);
}

// The input-capture edge detector watches P20 whatever its direction: a
// write to the data register toggles it when it is an output.
void Peripherals::p20_check() {
  const Port& p = port_[1];
  const bool level = (p.ddr & 1) ? (p.data & 1) != 0 : (p.pins & 1) != 0;
  if (level == p20_) return;
  p20_ = level;
  if (level != ((tcsr_ & kIedg) != 0)) return;
  // The counter is transferred on the second E edge after the transition.
  const u64 t = now();
  timer_sync(t);
  icr_ = frc_at(t + 1);
  tcsr_ |= kIcf;
  update_irq2();
}

// ---------------------------------------------------------------------------
// Timer

u64 Peripherals::next_ocf(u64 from) const {
  u64 t = cycle_with(from, ocr_);
  if (t >= ocf_inhibit_lo_ && t <= ocf_inhibit_hi_) t = cycle_with(ocf_inhibit_hi_ + 1, ocr_);
  return t;
}

// Apply the compare and overflow matches of the cycles before `t`: a flag
// set in cycle n is visible from cycle n + 1. Both flags are sticky and the
// output level register only ever takes OLVL, so one match of each is all
// a span can produce. A flag that appears is sampled into the IRQ2
// flip-flop at once: the access that synced it may clear it again within
// the same instruction (MC6801 RM figure 5-13).
void Peripherals::timer_sync(u64 t) {
  if (t <= synced_) return;
  const u8 before = tcsr_;
  if (next_ocf(synced_) < t) {
    tcsr_ |= kOcf;
    set_olr((tcsr_ & kOlvl) != 0);
  }
  if (next_tof(synced_) < t) tcsr_ |= kTof;
  synced_ = t;
  if (tcsr_ != before) update_irq2();
}

// The next transition anyone can see: a flag whose interrupt is enabled, or
// P21 about to change.
void Peripherals::timer_reschedule() {
  cancel(timer_event_);
  u64 when = kNever;
  const bool olr_change = (port_[1].ddr & 0x02) && olr_ != ((tcsr_ & kOlvl) != 0);
  if ((!(tcsr_ & kOcf) && (tcsr_ & kEoci)) || olr_change) when = next_ocf(synced_) + 1;
  if (!(tcsr_ & kTof) && (tcsr_ & kEtoi)) when = std::min(when, next_tof(synced_) + 1);
  if (when != kNever) timer_event_ = sched_.schedule(when, &on_timer, this);
}

void Peripherals::on_timer(void* self, u64 when, u64) {
  auto* p = static_cast<Peripherals*>(self);
  p->timer_event_ = 0;
  p->timer_sync(when);
  p->update_irq2();
  p->timer_reschedule();
}

void Peripherals::set_olr(bool level) {
  if (olr_ == level) return;
  olr_ = level;
  port_changed(1);
}

// A counter write in cycle t: the value holds from the next cycle, the
// compare is inhibited for both. The SCI's bit clock moves with it.
void Peripherals::counter_written(u64 t, u16 value) {
  timer_sync(t);
  frc_origin_ = t + 1 - value;
  frc_loaded_at_ = t + 1;
  ocf_inhibit_lo_ = t;
  ocf_inhibit_hi_ = t + 1;
  timer_reschedule();
}

u8 Peripherals::timer_status() {
  timer_sync(now());
  return tcsr_;
}

// ---------------------------------------------------------------------------
// Serial communication interface

bool Peripherals::sci_clock_running() const {
  const u8 cc = (rmcr_ >> 2) & 3;
  if (cc == 3) return ext_clock_q16_ != 0;
  // CC1:CC0 = 00 is the bi-phase format on the MC6801, no clock on the HD6301.
  return cc != 0 || family_ == Family::M6801;
}

u64 Peripherals::sci_halfbits(unsigned n) const {
  if (((rmcr_ >> 2) & 3) == 3) return (u64(n) * 4 * ext_clock_q16_ + 0x8000) >> 16;
  return u64(n) * (kRates[rmcr_ & 3] / 2);
}

// The internal bit clock divides the free-running counter.
u64 Peripherals::sci_next_edge(u64 t) const {
  if (((rmcr_ >> 2) & 3) == 3) {
    const u64 bit_q16 = u64(ext_clock_q16_) * 8;
    if (!bit_q16) return t + 1;
    const u64 k = ((t - ext_origin_) << 16) / bit_q16 + 1;
    return ext_origin_ + ((k * bit_q16 + 0xFFFF) >> 16);
  }
  const u64 div = kRates[rmcr_ & 3];
  return t + div - ((t - frc_origin_) & (div - 1));
}

bool Peripherals::sci_irq() const {
  return ((trcsr_ & kRie) && (trcsr_ & (kRdrf | kOrfe))) || ((trcsr_ & kTie) && (trcsr_ & kTdre));
}

void Peripherals::set_sci_external_clock(u32 period_q16) {
  const u64 t = now();
  sci_sync(t);
  ext_clock_q16_ = period_q16;
  ext_origin_ = t;
  sci_tx_schedule(t);
}

// Run the SCI transitions due by `t` (a register access inside an
// instruction that started before them).
void Peripherals::sci_sync(u64 t) {
  for (;;) {
    u64 at = kNever;
    int which = -1;
    if (tx_event_ && tx_event_at_ <= t) { at = tx_event_at_; which = 0; }
    if (rx_event_ && rx_event_at_ <= t && rx_event_at_ < at) { at = rx_event_at_; which = 1; }
    if (wu_event_ && wu_event_at_ <= t && wu_event_at_ < at) { at = wu_event_at_; which = 2; }
    if (which < 0) return;
    if (which == 0) { cancel(tx_event_); sci_tx_event(at); }
    else if (which == 1) { cancel(rx_event_); sci_rx_event(at); }
    else { cancel(wu_event_); trcsr_ &= u8(~kWu); }
  }
}

// TDR holds a byte and the transmitter is free: it is loaded at the next
// bit-clock edge, not before the preamble or the previous frame ends.
void Peripherals::sci_tx_schedule(u64 t) {
  if (tx_event_ || tx_busy_ || !(trcsr_ & kTe) || (trcsr_ & kTdre) || !sci_clock_running()) return;
  tx_event_at_ = std::max(tx_ready_at_, sci_next_edge(t));
  tx_event_ = sched_.schedule(tx_event_at_, &on_tx, this);
}

void Peripherals::sci_tx_load(u64 at) {
  tx_shift_ = tdr_;
  trcsr_ |= kTdre;
  tx_busy_ = true;
  tx_event_at_ = at + sci_halfbits(20);  // start bit, eight data bits, stop bit
  tx_event_ = sched_.schedule(tx_event_at_, &on_tx, this);
}

void Peripherals::sci_tx_event(u64 when) {
  if (tx_busy_) {
    tx_busy_ = false;
    tx_ready_at_ = when;
    if (tx_hook_) tx_hook_(tx_shift_);
  }
  if (!(trcsr_ & kTdre) && (trcsr_ & kTe)) sci_tx_load(when);  // back to back
  update_irq2();
}

void Peripherals::on_tx(void* self, u64 when, u64) {
  auto* p = static_cast<Peripherals*>(self);
  p->tx_event_ = 0;
  p->sci_tx_event(when);
}

void Peripherals::receive(const u8* data, size_t len) {
  const u64 t = now();
  if (!(trcsr_ & kRe) || !sci_clock_running()) return;
  sci_sync(t);
  rx_queue_.insert(rx_queue_.end(), data, data + len);
  sci_rx_start(t);
}

void Peripherals::sci_rx_start(u64 t) {
  if (rx_busy_ || rx_queue_.empty()) return;
  rx_shift_ = rx_queue_.front();
  rx_queue_.pop_front();
  rx_busy_ = true;
  rx_start_ = std::max(t, rx_line_free_);
  cancel(wu_event_);
  rx_event_at_ = rx_start_ + sci_halfbits(19);  // the stop bit is sampled in its middle
  rx_event_ = sched_.schedule(rx_event_at_, &on_rx, this);
}

void Peripherals::sci_rx_event(u64 when) {
  rx_busy_ = false;
  rx_line_free_ = rx_start_ + sci_halfbits(20);
  // The line is a mark from the first of the trailing ones (the high data
  // bits that are set, then the stop bit): what wake-up counts.
  unsigned ones = 1;
  for (int b = 7; b >= 0 && ((rx_shift_ >> b) & 1); --b) ++ones;
  rx_idle_since_ = rx_line_free_ - sci_halfbits(2 * ones);
  if (trcsr_ & kWu) {
    // Asleep: the frame sets no flag.
  } else if (trcsr_ & kRdrf) {
    trcsr_ |= kOrfe;  // overrun: the byte is lost, RDR keeps the old one
  } else {
    rdr_ = rx_shift_;
    trcsr_ |= kRdrf;
  }
  update_irq2();
  if (!rx_queue_.empty()) sci_rx_start(rx_line_free_);
  else sci_wu_schedule(when);
}

void Peripherals::on_rx(void* self, u64 when, u64) {
  auto* p = static_cast<Peripherals*>(self);
  p->rx_event_ = 0;
  p->sci_rx_event(when);
}

// Wake-up clears after ten consecutive ones on the receive line.
void Peripherals::sci_wu_schedule(u64 t) {
  cancel(wu_event_);
  if (!(trcsr_ & kWu) || rx_busy_ || !rx_queue_.empty() || !sci_clock_running()) return;
  wu_event_at_ = std::max(rx_idle_since_ + sci_halfbits(20), sci_next_edge(t));
  wu_event_ = sched_.schedule(wu_event_at_, &on_wu, this);
}

void Peripherals::on_wu(void* self, u64, u64) {
  auto* p = static_cast<Peripherals*>(self);
  p->wu_event_ = 0;
  p->trcsr_ &= u8(~kWu);
}

}  // namespace m6801

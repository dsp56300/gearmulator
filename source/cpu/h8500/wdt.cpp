#include "cpu/h8500/wdt.hpp"

#include "common/iomux.hpp"

namespace h8500 {

Wdt::Wdt(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 tcsr_addr, u32 rstcsr_addr)
    : sched_(sched), clock_(clock), intc_(intc), tcsr_addr_(tcsr_addr), rstcsr_addr_(rstcsr_addr) {}

Wdt::~Wdt() {
  if (event_) sched_.cancel(event_);
}

void Wdt::map(emu::IoMux& mux) {
  mux.assign(tcsr_addr_, 2, this);
  // The H8/532 has no RSTCSR: its reset enable lives in TCSR (address 0 here).
  if (rstcsr_addr_) mux.assign(rstcsr_addr_, 2, this);
}

void Wdt::reset(bool by_watchdog) {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  tcsr_ = 0;
  tcnt_ = 0;
  if (!by_watchdog) rstcsr_ = 0;
  ovf_read_ = false;
  tick_ = 0;
  now_ = 0;
  update_request();
}

unsigned Wdt::period_shift() const {
  static constexpr u8 kShift[8] = {1, 5, 6, 7, 8, 9, 11, 12};
  return kShift[tcsr_ & 7];
}

// ---------------------------------------------------------------------------

void Wdt::sync(u64 now) {
  if (now <= now_) return;
  now_ = now;
  if (!running()) return;
  const u64 cur = tick_of(now);
  while (tick_ < cur) {
    const u64 d = u64(u8(0xFF - tcnt_)) + 1;  // ticks until the H'FF -> H'00 transition
    if (tick_ + d > cur) break;
    tick_ += d;
    tcnt_ = 0;
    overflow();
    if (!running()) { tick_ = cur; return; }  // a watchdog reset stopped the timer
  }
  tcnt_ = u8(tcnt_ + (cur - tick_));
  tick_ = cur;
}

void Wdt::overflow() {
  if (tcsr_ & kWtIt) {
    // Watchdog mode: internal reset for the whole chip, WRST records the cause.
    rstcsr_ |= kWrst;
    if (reset_sink_) reset_sink_->watchdog_reset();
    else reset(true);
  } else {
    tcsr_ |= kOvf;
    update_request();
  }
}

void Wdt::update_request() {
  intc_.set_request(IrqSrc::Wdt, (tcsr_ & kOvf) && !(tcsr_ & kWtIt));
}

void Wdt::reschedule() {
  if (event_) { sched_.cancel(event_); event_ = 0; }
  if (!running()) return;
  const u64 d = u64(u8(0xFF - tcnt_)) + 1;
  event_ = sched_.schedule((tick_ + d) << period_shift(), &Wdt::on_event, this);
}

void Wdt::on_event(void* self, u64 /*when*/, u64 now) {
  auto* w = static_cast<Wdt*>(self);
  w->event_ = 0;
  w->sync(now);
  w->reschedule();
}

// ---------------------------------------------------------------------------
// Registers

u8 Wdt::read8(u32 addr) {
  sync(clock_.now());
  if (addr == tcsr_addr_) {
    if (tcsr_ & kOvf) ovf_read_ = true;
    return u8(tcsr_ | 0x18);
  }
  if (addr == tcsr_addr_ + 1) return tcnt_;
  if (addr == rstcsr_addr_ + 1) return u8(rstcsr_ | 0x3F);
  return 0xFF;
}

void Wdt::write8(u32 /*addr*/, u8 /*value*/) {
  // TCNT, TCSR and RSTCSR cannot be written by byte access.
}

void Wdt::write16(u32 addr, u16 value) {
  sync(clock_.now());
  const u8 key = u8(value >> 8), data = u8(value);
  if (addr == tcsr_addr_) {
    if (key == 0xA5) {
      // OVF clears only when written 0 after having been read as 1; it can
      // never be set by software.  Clearing TME stops and zeroes the counter.
      u8 next = u8((tcsr_ & kOvf) | (data & 0x67));
      if (!(data & kOvf) && ovf_read_) next &= u8(~kOvf);
      ovf_read_ = false;
      const bool was_running = running();
      const unsigned old_shift = period_shift();
      tcsr_ = next;
      if (!running()) tcnt_ = 0;
      else if (!was_running || period_shift() != old_shift) tick_ = tick_of(now_);
      update_request();
      reschedule();
    } else if (key == 0x5A) {
      tcnt_ = data;
      reschedule();
    }
    // Any other password: neither register changes.
  } else if (addr == rstcsr_addr_) {
    if (key == 0xA5 && data == 0x00) rstcsr_ &= u8(~kWrst);
    else if (key == 0x5A) rstcsr_ = u8((rstcsr_ & ~kRstoe) | (data & kRstoe));
  }
}

}  // namespace h8500

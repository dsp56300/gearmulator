#include "cpu/sh2/wdt.hpp"

namespace sh2 {

Wdt::Wdt(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base)
    : base_(base), sched_(sched), clock_(clock), intc_(intc) {
  reset();
}

Wdt::~Wdt() {
  if (event_) sched_.cancel(event_);
}

void Wdt::map(emu::IoMux& mux) { mux.assign(base_, 4, this); }

void Wdt::reset(bool by_watchdog) {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  tcsr_ = 0;
  tcnt_ = 0;
  if (!by_watchdog) {
    rstcsr_ = 0;  // RES pin / power-on only (11.2.3)
    wovf_read_ = false;
    wdtovf_at_ = ~u64(0);
  }
  ovf_read_ = false;
  tick_ = 0;
  now_ = 0;
  update_request();
}

unsigned Wdt::period_shift() const {
  static constexpr u8 kShift[8] = {1, 6, 7, 8, 9, 10, 12, 13};  // phi/2 ... phi/8192
  return kShift[tcsr_ & kCksMask];
}

// ---------------------------------------------------------------------------
// Counter model: one count at every multiple of 2^shift states while TME = 1.

void Wdt::sync(u64 now) {
  if (now <= now_) return;
  now_ = now;
  if (!running()) return;
  const u64 cur = tick_of(now);
  const u64 elapsed = cur - tick_;
  const u64 to_ovf = 0x100 - tcnt_;  // counts until the H'FF -> H'00 transition
  if (to_ovf <= elapsed) {
    const u64 at = (tick_ + to_ovf) << period_shift();
    tick_ = cur;
    tcnt_ = 0;
    overflow(at);
    // Interval mode keeps counting (later overflows only re-set OVF); a
    // watchdog overflow has reset TCSR and stopped the counter at H'00.
    if (running()) tcnt_ = u8(elapsed - to_ovf);
    return;
  }
  tcnt_ = u8(tcnt_ + elapsed);
  tick_ = cur;
}

void Wdt::overflow(u64 at) {
  if (!(tcsr_ & kWtIt)) {
    tcsr_ |= kOvf;  // 11.3.4: OVF and the ITI request together
    update_request();
    return;
  }
  // Watchdog mode (11.3.1, 11.3.5, 11.4.5): WOVF, WDTOVF pulse, module reset,
  // then the chip-wide internal reset when RSTE is set.
  rstcsr_ |= kWovf;
  wdtovf_at_ = at;
  tcsr_ = 0;
  tcnt_ = 0;
  ovf_read_ = false;
  update_request();
  if (wdtovf_) wdtovf_(at);
  if ((rstcsr_ & kRste) && reset_sink_) reset_sink_->watchdog_reset();
}

void Wdt::update_request() {
  // ITI has no enable bit of its own: requested while OVF is set (IPRH masks it).
  intc_.set_request(IrqSrc::Iti, (tcsr_ & kOvf) != 0);
}

// The event marks the next overflow that does something: always in watchdog
// mode, and in interval mode while OVF is clear (a second overflow with OVF
// already set changes nothing the program cannot derive from the clock).
void Wdt::reschedule() {
  if (event_) { sched_.cancel(event_); event_ = 0; }
  if (!running()) return;
  if (!(tcsr_ & kWtIt) && (tcsr_ & kOvf)) return;
  const u64 to_ovf = 0x100 - tcnt_;
  event_ = sched_.schedule((tick_ + to_ovf) << period_shift(), &Wdt::on_event, this);
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
  switch (addr - base_) {
    case 0:  // TCSR
      if (tcsr_ & kOvf) ovf_read_ = true;
      return u8(tcsr_ | 0x18);  // bits 4-3 read as 1
    case 1: return tcnt_;  // TCNT
    case 3:  // RSTCSR
      if (rstcsr_ & kWovf) wovf_read_ = true;
      return u8(rstcsr_ | 0x1F);  // bits 4-0 read as 1, bit 5 as 0
    default: return 0xFF;  // H'FFFF8612 is the RSTCSR write address only (table 11.2)
  }
}

void Wdt::write8(u32 /*addr*/, u8 /*value*/) {
  // TCNT, TCSR and RSTCSR cannot be written by byte access (11.2.4).
}

void Wdt::write32(u32 /*addr*/, u32 /*value*/) {
  // Nor by longword access (table 11.2, note 1).
}

void Wdt::write16(u32 addr, u16 value) {
  sync(clock_.now());
  const u8 key = u8(value >> 8), data = u8(value);
  if (addr == base_) {
    if (key == 0xA5) {
      // OVF clears only when written 0 after having been read as 1; it can
      // never be set by software.  Clearing TME stops and zeroes the counter.
      u8 next = u8((tcsr_ & kOvf) | (data & (kWtIt | kTme | kCksMask)));
      if (!(data & kOvf) && ovf_read_) next &= u8(~kOvf);
      ovf_read_ = false;
      const bool was_running = running();
      const unsigned old_shift = period_shift();
      tcsr_ = next;
      if (!running()) tcnt_ = 0;
      else if (!was_running || period_shift() != old_shift) tick_ = tick_of(now_);  // counts at the next boundary
      update_request();
      reschedule();
    } else if (key == 0x5A) {
      // 11.4.1: a count in the write cycle loses to the write (sync() above
      // applied it first, the value now replaces it).
      tcnt_ = data;
      reschedule();
    }
    // Any other password: neither register changes.
  } else if (addr == base_ + 2) {
    if (key == 0xA5 && data == 0x00) {
      if (wovf_read_) rstcsr_ &= u8(~kWovf);  // read 1 then write 0 (11.2.3)
      wovf_read_ = false;
    } else if (key == 0x5A) {
      rstcsr_ = u8((rstcsr_ & kWovf) | (data & kRste));
    }
  }
}

}  // namespace sh2

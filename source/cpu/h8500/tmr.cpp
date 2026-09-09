#include "cpu/h8500/tmr.hpp"

#include <algorithm>

#include "common/iomux.hpp"

namespace h8500 {

namespace {
enum Reg : u32 { kTcr = 0, kTcsr = 1, kTcora = 2, kTcorb = 3, kTcnt = 4 };
constexpr u64 kNoTick = ~u64(0);
}  // namespace

Tmr::Tmr(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base, Sources src)
    : sched_(sched), clock_(clock), intc_(intc), base_(base), src_(src) {}

Tmr::~Tmr() {
  if (event_) sched_.cancel(event_);
}

void Tmr::map(emu::IoMux& mux) { mux.assign(base_, 5, this); }

void Tmr::reset() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  tcr_ = 0;
  tcsr_ = 0;
  flags_read_ = 0;
  tcora_ = tcorb_ = 0xFF;
  tcnt_ = 0;
  tick_ = 0;
  now_ = 0;
  out_ = false;
  update_requests();
}

unsigned Tmr::period_shift() const {
  switch (tcr_ & 7) {
    case 1: return 3;   // phi/8
    case 2: return 6;   // phi/64
    case 3: return 10;  // phi/1024
    default: return 0;  // stopped (0, 4) or external (5-7)
  }
}

// ---------------------------------------------------------------------------
// Counter model

u64 Tmr::ticks_to_next_special() const {
  const u64 a = u64(u8(tcora_ - tcnt_)) + 1;
  const u64 b = u64(u8(tcorb_ - tcnt_)) + 1;
  const u64 o = u64(u8(0xFF - tcnt_)) + 1;
  return std::min({a, b, o});
}

void Tmr::transition() {
  const bool match_a = tcnt_ == tcora_;
  const bool match_b = tcnt_ == tcorb_;
  const unsigned cclr = (tcr_ >> 3) & 3;
  const bool clear = (cclr == 1 && match_a) || (cclr == 2 && match_b);
  const bool overflow = tcnt_ == 0xFF && !clear;
  if (match_a) tcsr_ |= kCmfa;
  if (match_b) tcsr_ |= kCmfb;
  if (overflow) tcsr_ |= kOvf;
  // Timer output: the OS field of each matching comparator requests an
  // action; when both match, toggle > 1 > 0 > no change (table 11-4).
  unsigned action = 0;
  if (match_a) action = std::max(action, unsigned(tcsr_ & 3));
  if (match_b) action = std::max(action, unsigned((tcsr_ >> 2) & 3));
  switch (action) {
    case 1: out_ = false; break;
    case 2: out_ = true; break;
    case 3: out_ = !out_; break;
    default: break;
  }
  tcnt_ = clear ? 0 : u8(tcnt_ + 1);
}

void Tmr::sync(u64 now) {
  if (now <= now_) return;
  now_ = now;
  if (!internal_clock()) return;
  const u64 cur = tick_of(now);
  while (tick_ < cur) {
    const u64 d = ticks_to_next_special();
    if (tick_ + d > cur) break;
    tcnt_ = u8(tcnt_ + (d - 1));
    tick_ += d;
    transition();
  }
  tcnt_ = u8(tcnt_ + (cur - tick_));
  tick_ = cur;
  update_requests();
}

void Tmr::update_requests() {
  intc_.set_request(src_.cmia, (tcsr_ & kCmfa) && (tcr_ & kCmiea));
  intc_.set_request(src_.cmib, (tcsr_ & kCmfb) && (tcr_ & kCmieb));
  intc_.set_request(src_.ovi, (tcsr_ & kOvf) && (tcr_ & kOvie));
}

void Tmr::reschedule() {
  if (event_) { sched_.cancel(event_); event_ = 0; }
  if (!internal_clock()) return;
  u64 best = kNoTick;
  auto consider = [&](u8 v, bool enabled) {
    if (enabled) best = std::min(best, u64(u8(v - tcnt_)) + 1);
  };
  consider(tcora_, (tcr_ & kCmiea) && !(tcsr_ & kCmfa));
  consider(tcorb_, (tcr_ & kCmieb) && !(tcsr_ & kCmfb));
  consider(0xFF, (tcr_ & kOvie) && !(tcsr_ & kOvf));
  if (best == kNoTick) return;
  event_ = sched_.schedule((tick_ + best) << period_shift(), &Tmr::on_event, this);
}

void Tmr::on_event(void* self, u64 /*when*/, u64 now) {
  auto* t = static_cast<Tmr*>(self);
  t->event_ = 0;
  t->sync(now);
  t->reschedule();
}

// ---------------------------------------------------------------------------
// Pins

void Tmr::external_clock_edge(bool rising) {
  const unsigned cks = tcr_ & 7;
  const bool counts = (cks == 5 && rising) || (cks == 6 && !rising) || cks == 7;
  if (!counts) return;
  transition();
  update_requests();
}

void Tmr::external_reset_edge() {
  if (((tcr_ >> 3) & 3) != 3) return;
  sync(clock_.now());
  tcnt_ = 0;
  reschedule();
}

void Tmr::dtc_clear(IrqSrc src) {
  if (src == src_.cmia) tcsr_ &= u8(~kCmfa);
  else if (src == src_.cmib) tcsr_ &= u8(~kCmfb);
  update_requests();
  reschedule();
}

// ---------------------------------------------------------------------------
// Registers

u8 Tmr::read8(u32 addr) {
  sync(clock_.now());
  switch (addr - base_) {
    case kTcr: return tcr_;
    case kTcsr:
      flags_read_ |= u8(tcsr_ & 0xE0);
      return u8(tcsr_ | 0x10);  // bit 4 reads as 1
    case kTcora: return tcora_;
    case kTcorb: return tcorb_;
    case kTcnt: return tcnt_;
    default: return 0xFF;
  }
}

void Tmr::write8(u32 addr, u8 v) {
  sync(clock_.now());
  switch (addr - base_) {
    case kTcr: {
      const unsigned old_shift = period_shift();
      tcr_ = v;
      if (period_shift() != old_shift && internal_clock()) tick_ = tick_of(now_);
      update_requests();
      reschedule();
      break;
    }
    case kTcsr: {
      u8 cleared = 0;
      for (u8 bit = 0x20; bit; bit <<= 1)
        if (!(v & bit) && (flags_read_ & bit)) cleared |= bit;
      tcsr_ = u8((tcsr_ & 0xE0 & ~cleared) | (v & 0x0F));
      flags_read_ &= u8(~cleared);
      update_requests();
      reschedule();
      break;
    }
    case kTcora: tcora_ = v; reschedule(); break;
    case kTcorb: tcorb_ = v; reschedule(); break;
    case kTcnt: tcnt_ = v; reschedule(); break;
    default: break;
  }
}

}  // namespace h8500

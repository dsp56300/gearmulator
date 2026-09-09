#include "cpu/h8500/frt.hpp"

#include <algorithm>

#include "common/iomux.hpp"

namespace h8500 {

namespace {
enum Reg : u32 { kTcr = 0, kTcsr = 1, kFrcH = 2, kFrcL = 3, kOcraH = 4, kOcraL = 5, kOcrbH = 6, kOcrbL = 7, kIcrH = 8, kIcrL = 9 };
constexpr u64 kNoTick = ~u64(0);
}  // namespace

Frt::Frt(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base, Sources src)
    : sched_(sched), clock_(clock), intc_(intc), base_(base), src_(src) {}

Frt::~Frt() {
  if (event_) sched_.cancel(event_);
}

void Frt::map(emu::IoMux& mux) { mux.assign(base_, 10, this); }

void Frt::reset() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  tcr_ = 0;
  tcsr_ = 0;
  flags_read_ = 0;
  frc_ = 0;
  ocra_ = ocrb_ = 0xFFFF;
  icr_ = 0;
  temp_ = 0;
  tick_ = 0;
  now_ = 0;
  out_a_ = out_b_ = false;
  update_requests();
}

unsigned Frt::period_shift() const {
  switch (tcr_ & 3) {
    case 0: return 2;   // phi/4
    case 1: return 3;   // phi/8
    case 2: return 5;   // phi/32
    default: return 0;  // external
  }
}

// ---------------------------------------------------------------------------
// Counter model

u64 Frt::ticks_to_next_special() const {
  // The transition off value v happens (v - frc_) mod 2^16 + 1 ticks from now:
  // the count reaches v after (v - frc_) ticks and the match/overflow signal is
  // generated on the following increment pulse.
  const u64 a = u64(u16(ocra_ - frc_)) + 1;
  const u64 b = u64(u16(ocrb_ - frc_)) + 1;
  const u64 o = u64(u16(0xFFFF - frc_)) + 1;
  return std::min({a, b, o});
}

void Frt::transition() {
  // Called when the counter holds frc_ and the increment pulse arrives.
  const bool match_a = frc_ == ocra_;
  const bool match_b = frc_ == ocrb_;
  const bool clear = match_a && (tcsr_ & kCclra);
  const bool overflow = frc_ == 0xFFFF && !clear;
  if (match_a) {
    tcsr_ |= kOcfa;
    if (tcr_ & kOea) out_a_ = (tcsr_ & kOlvla) != 0;
  }
  if (match_b) {
    tcsr_ |= kOcfb;
    if (tcr_ & kOeb) out_b_ = (tcsr_ & kOlvlb) != 0;
  }
  if (overflow) tcsr_ |= kOvf;
  frc_ = clear ? 0 : u16(frc_ + 1);
}

void Frt::sync(u64 now) {
  if (now <= now_) return;
  now_ = now;
  if (!internal_clock()) return;  // external pulses drive the count directly
  const u64 cur = tick_of(now);
  while (tick_ < cur) {
    // The clear on match A always matters for the count itself, so every
    // special (not only interrupt-enabled ones) is replayed here.
    const u64 d = ticks_to_next_special();
    if (tick_ + d > cur) break;
    frc_ = u16(frc_ + (d - 1));  // reach the special value
    tick_ += d;                  // ... and take its transition on tick `tick_`
    transition();
  }
  frc_ = u16(frc_ + (cur - tick_));
  tick_ = cur;
  update_requests();
}

void Frt::update_requests() {
  intc_.set_request(src_.ici, (tcsr_ & kIcf) && (tcr_ & kIcie));
  intc_.set_request(src_.ocia, (tcsr_ & kOcfa) && (tcr_ & kOciea));
  intc_.set_request(src_.ocib, (tcsr_ & kOcfb) && (tcr_ & kOcieb));
  intc_.set_request(src_.fovi, (tcsr_ & kOvf) && (tcr_ & kOvie));
}

void Frt::reschedule() {
  if (event_) { sched_.cancel(event_); event_ = 0; }
  if (!internal_clock()) return;
  // Only transitions that can raise an interrupt need an event; flag-only
  // transitions are recovered lazily.  Interrupts whose flag is already set
  // have been requested by update_requests().
  bool want = false;
  u64 best = kNoTick;
  auto consider = [&](u16 v, bool enabled) {
    if (!enabled) return;
    want = true;
    best = std::min(best, u64(u16(v - frc_)) + 1);
  };
  consider(ocra_, (tcr_ & kOciea) && !(tcsr_ & kOcfa));
  consider(ocrb_, (tcr_ & kOcieb) && !(tcsr_ & kOcfb));
  consider(0xFFFF, (tcr_ & kOvie) && !(tcsr_ & kOvf));
  if (!want) return;
  const u64 when = (tick_ + best) << period_shift();
  event_ = sched_.schedule(when, &Frt::on_event, this);
}

void Frt::on_event(void* self, u64 /*when*/, u64 now) {
  auto* f = static_cast<Frt*>(self);
  f->event_ = 0;
  f->sync(now);
  f->reschedule();
}

// ---------------------------------------------------------------------------
// Pins

void Frt::capture_edge(bool rising) {
  if (rising != ((tcsr_ & kIedg) != 0)) return;
  sync(clock_.now());
  // The capture copies the count even if ICF is already set.
  icr_ = frc_;
  tcsr_ |= kIcf;
  update_requests();
}

void Frt::external_clock_pulse() {
  if (internal_clock()) return;
  transition();  // handles match / clear / overflow for the value being left
  update_requests();
}

void Frt::dtc_clear(IrqSrc src) {
  if (src == src_.ici) tcsr_ &= u8(~kIcf);
  else if (src == src_.ocia) tcsr_ &= u8(~kOcfa);
  else if (src == src_.ocib) tcsr_ &= u8(~kOcfb);
  update_requests();
  reschedule();
}

// ---------------------------------------------------------------------------
// Registers

u8 Frt::read8(u32 addr) {
  sync(clock_.now());  // count and flags as of the accessing instruction
  switch (addr - base_) {
    case kTcr: return tcr_;
    case kTcsr:
      flags_read_ |= u8(tcsr_ & 0xF0);
      return tcsr_;
    case kFrcH: temp_ = u8(frc_); return u8(frc_ >> 8);
    case kFrcL: return temp_;
    case kOcraH: return u8(ocra_ >> 8);  // OCR reads bypass TEMP
    case kOcraL: return u8(ocra_);
    case kOcrbH: return u8(ocrb_ >> 8);
    case kOcrbL: return u8(ocrb_);
    case kIcrH: temp_ = u8(icr_); return u8(icr_ >> 8);
    case kIcrL: return temp_;
    default: return 0xFF;
  }
}

void Frt::write8(u32 addr, u8 v) {
  sync(clock_.now());
  switch (addr - base_) {
    case kTcr: {
      // tick_ is expressed in the old clock's units: re-base it when the
      // prescaler changes (the changeover glitch of table 10-5 is not modelled).
      const unsigned old_shift = period_shift();
      tcr_ = v;
      if (period_shift() != old_shift && internal_clock()) tick_ = tick_of(now_);
      // A newly enabled interrupt whose flag is already set requests at once.
      update_requests();
      reschedule();
      break;
    }
    case kTcsr: {
      // Bits 3-0 are plain R/W; a flag clears when written 0 after having
      // been read as 1 (and 1s cannot be written).
      u8 cleared = 0;
      for (u8 bit = 0x10; bit; bit <<= 1)
        if (!(v & bit) && (flags_read_ & bit)) cleared |= bit;
      tcsr_ = u8((tcsr_ & 0xF0 & ~cleared) | (v & 0x0F));
      flags_read_ &= u8(~cleared);
      update_requests();
      reschedule();
      break;
    }
    case kFrcH: case kOcraH: case kOcrbH:
      temp_ = v;
      break;
    case kFrcL:
      frc_ = u16((u16(temp_) << 8) | v);
      reschedule();
      break;
    case kOcraL:
      ocra_ = u16((u16(temp_) << 8) | v);
      reschedule();
      break;
    case kOcrbL:
      ocrb_ = u16((u16(temp_) << 8) | v);
      reschedule();
      break;
    default:  // ICR is read-only
      break;
  }
}

}  // namespace h8500

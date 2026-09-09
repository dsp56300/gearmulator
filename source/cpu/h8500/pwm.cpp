#include "cpu/h8500/pwm.hpp"

#include "common/iomux.hpp"

namespace h8500 {

Pwm::Pwm(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base)
    : sched_(sched), clock_(clock), intc_(intc), base_(base) {}

Pwm::~Pwm() {
  if (event_) sched_.cancel(event_);
}

void Pwm::map(emu::IoMux& mux) { mux.assign(base_, 14, this); }

void Pwm::reset() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  tcr_ = 0;
  tmsr_ = 0;
  odl_ = 0;
  for (u8& o : odr_) o = 0;
  for (u16& o : ocr_) o = 0xFFFF;
  tmr_ = 0;
  temp_ = 0;
  ocf_read_ = 0;
  tick_ = clock_.now();
  update_requests();
}

u32 Pwm::ticks_to_next_match() const {
  u32 k = 0x10000;
  for (unsigned i = 0; i < 3; ++i) {
    // Match i is acknowledged on the tick after TMR == OCRi.
    const u32 ki = ((u32(ocr_[i]) - tmr_) & 0xFFFF) + 1;
    if (ki < k) k = ki;
  }
  return k;
}

// Bring TMR up to `now`, processing every match on the way.
void Pwm::sync(u64 now) {
  if (!running()) { tick_ = now; return; }
  const unsigned sh = shift();
  for (;;) {
    const u64 ticks = (now - tick_) >> sh;
    if (!ticks) return;
    const u32 k = ticks_to_next_match();
    if (ticks < k) {
      tmr_ = u16(tmr_ + ticks);
      tick_ += ticks << sh;
      return;
    }
    // Advance to the tick after the match.
    const u16 at = u16(tmr_ + k - 1);  // counter value that matched
    tick_ += u64(k) << sh;
    tmr_ = u16(at + 1);
    for (unsigned i = 0; i < 3; ++i)
      if (ocr_[i] == at) match(i);
  }
}

void Pwm::match(unsigned i) {
  // ODRi -> ODL: one bit (bit i) or all six (OMS); OCR0 wins simultaneous
  // parallel transfers, so apply in ascending order and let 0 overwrite.
  if (tcr_ & kOms) odl_ = u8(odr_[i] & 0x3F);
  else odl_ = u8((odl_ & ~(1u << i)) | (odr_[i] & (1u << i)));
  tmsr_ |= u8(1u << i);
  if (i == 0 && (tcr_ & kFrm)) tmr_ = 0;
  if ((i == 0 && (tmsr_ & kTre0)) || (i == 2 && (tmsr_ & kTre2)))
    if (route_hook_) route_hook_(i);
  update_requests();
}

void Pwm::update_requests() {
  intc_.set_request(IrqSrc::PwmOcf0, (tmsr_ & kOcie0) && (tmsr_ & kOcf0));
  intc_.set_request(IrqSrc::PwmOcf1, (tmsr_ & kOcie1) && (tmsr_ & kOcf1));
  intc_.set_request(IrqSrc::PwmOcf2, (tmsr_ & kOcie2) && (tmsr_ & kOcf2));
}

void Pwm::reschedule() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  if (!running()) return;
  const u64 when = tick_ + (u64(ticks_to_next_match()) << shift());
  event_ = sched_.schedule(when, &Pwm::on_event, this);
}

void Pwm::on_event(void* self, u64 /*when*/, u64 now) {
  auto* p = static_cast<Pwm*>(self);
  p->event_ = 0;
  p->sync(now);
  p->reschedule();
}

u8 Pwm::read8(u32 addr) {
  const u32 off = addr - base_;
  switch (off) {
    case 0: return tcr_;
    case 1: ocf_read_ = u8(tmsr_ & 0x07); return tmsr_;
    case 2: return u8(odl_ | 0xC0);
    case 3: case 4: case 5: return u8(odr_[off - 3] | 0xC0);
    case 6: case 8: case 10: return u8(ocr_[(off - 6) / 2] >> 8);
    case 7: case 9: case 11: return u8(ocr_[(off - 7) / 2]);
    case 12: sync(clock_.now()); temp_ = u8(tmr_); return u8(tmr_ >> 8);
    case 13: return temp_;
    default: return 0xFF;
  }
}

void Pwm::write8(u32 addr, u8 v) {
  const u32 off = addr - base_;
  sync(clock_.now());
  switch (off) {
    case 0:
      tcr_ = v;
      tick_ = clock_.now();  // a stopped or re-clocked counter restarts from here
      break;
    case 1:
      // OCFn clear by writing 0 after reading 1; the other bits are plain.
      tmsr_ = u8((v & 0xF8) | (tmsr_ & 0x07 & (v | u8(~ocf_read_))));
      ocf_read_ = 0;
      update_requests();
      break;
    case 2: odl_ = v & 0x3F; break;
    case 3: case 4: case 5: odr_[off - 3] = v & 0x3F; break;
    case 6: case 8: case 10: case 12: temp_ = v; break;  // high byte staged
    case 7: case 9: case 11: ocr_[(off - 7) / 2] = u16((temp_ << 8) | v); break;
    case 13: tmr_ = u16((temp_ << 8) | v); break;
    default: break;
  }
  reschedule();
}

void Pwm::write16(u32 addr, u16 value) {
  write8(addr, u8(value >> 8));
  write8(addr + 1, u8(value));
}

}  // namespace h8500

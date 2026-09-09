#include "cpu/h8500/adc.hpp"

#include "common/iomux.hpp"

namespace h8500 {

namespace {
constexpr u32 kAdcsr = 8, kAdcr = 9;
}  // namespace

Adc::Adc(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base)
    : sched_(sched), clock_(clock), intc_(intc), base_(base) {}

Adc::~Adc() {
  if (event_) sched_.cancel(event_);
}

void Adc::map(emu::IoMux& mux) { mux.assign(base_, 10, this); }

void Adc::reset() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  for (u16& v : addr_) v = 0;
  adcsr_ = 0;
  adcr_ = 0;
  temp_ = 0;
  channel_ = 0;
  end_ = 0;
  now_ = 0;
  update_request();
}

// ---------------------------------------------------------------------------
// Lazy conversion model.  `end_` is the completion time of the conversion of
// `channel_` in progress.  Nothing ticks per conversion: register accesses
// (and the host) resolve every conversion that has completed since the last
// visit, and the only scheduler event is the one whose completion changes
// something a program can see without reading the module -- ADF rising (the
// ADI request) and, in single mode, ADST clearing.  Scan rounds that only
// refresh the result registers while ADF is already set run without events.
// Samples are taken when the conversion is resolved, not at its own instant;
// for host inputs that change rarely the difference is a few hundred states.

u64 Adc::conversion_states(bool first) const {
  // Table 14-4: first conversion 266 / 134 states (t_D + t_SPL + conversion),
  // subsequent scan-mode conversions 256 / 128.
  return (adcsr_ & kCks) ? (first ? 134 : 128) : (first ? 266 : 256);
}

void Adc::sync(u64 now) {
  if (now < now_) return;
  now_ = now;
  if (!end_ || now < end_) return;
  if (!(adcsr_ & kScan)) {
    // Single conversion: one result, ADF, ADST cleared.
    addr_[result_index()] = u16(sample(channel_) << 6);
    adcsr_ |= kAdf;
    adcsr_ &= u8(~kAdst);
    end_ = 0;
    update_request();
    return;
  }
  // Scan: `n` conversions completed since end_, cycling through the group.
  const u64 t = conversion_states(false);
  const u64 n = (now - end_) / t + 1;
  const unsigned first = first_channel(), last = last_channel(), g = last - first + 1;
  const unsigned pos = channel_ - first;  // position of the conversion completing at end_
  const u64 wraps = (pos + n) / g;        // completions of the group's last channel
  const u64 done = n < g ? n : g;         // distinct channels with a fresh result
  for (u64 i = 0; i < done; ++i) {
    // The last `done` completions, in order, so each register holds its latest conversion.
    const unsigned ch = first + unsigned((pos + n - done + i) % g);
    addr_[ch & 3] = u16(sample(ch) << 6);
  }
  if (wraps) adcsr_ |= kAdf;
  channel_ = first + unsigned((pos + n) % g);
  end_ += n * t;
  update_request();
}

u16 Adc::sample(unsigned ch) const { return sampler_ ? u16(sampler_(ch) & 0x3FF) : inputs_[ch & 3]; }

void Adc::start(u64 at) {
  end_ = at + conversion_states(true);
  reschedule();
}

void Adc::stop() {
  end_ = 0;
  reschedule();
}

// One event for the next completion that has a visible effect.
void Adc::reschedule() {
  if (event_) { sched_.cancel(event_); event_ = 0; }
  if (!end_) return;
  u64 at = end_;
  if (adcsr_ & kScan) {
    if (adcsr_ & kAdf) return;  // further rounds only refresh results
    at += u64(last_channel() - channel_) * conversion_states(false);  // the group's last channel completes
  }
  event_ = sched_.schedule(at, &Adc::on_event, this);
}

void Adc::on_event(void* self, u64 /*when*/, u64 now) {
  auto* a = static_cast<Adc*>(self);
  a->event_ = 0;
  a->sync(now);
  a->reschedule();
}

void Adc::update_request() { intc_.set_request(IrqSrc::Adi, (adcsr_ & kAdf) && (adcsr_ & kAdie)); }

void Adc::trigger() {
  const u64 now = clock_.now();
  sync(now);
  if (!(adcr_ & kTrge) || (adcsr_ & kAdst)) return;
  adcsr_ |= kAdst;
  channel_ = u8(first_channel());
  start(now);
}

void Adc::dtc_clear() {
  sync(clock_.now());
  adcsr_ &= u8(~kAdf);
  update_request();
  reschedule();
}

// ---------------------------------------------------------------------------
// Registers

u8 Adc::read8(u32 addr) {
  sync(clock_.now());
  const u32 off = addr - base_;
  if (off < 8) {
    const u16 r = addr_[off >> 1];
    if ((off & 1) == 0) { temp_ = u8(r); return u8(r >> 8); }
    return temp_;
  }
  if (off == kAdcsr) {
    return adcsr_;
  }
  if (off == kAdcr) return u8(adcr_ | 0x7F);
  return 0xFF;
}

void Adc::write8(u32 addr, u8 v) {
  const u64 now = clock_.now();
  sync(now);
  const u32 off = addr - base_;
  if (off == kAdcsr) {
    const bool was_running = (adcsr_ & kAdst) != 0;
    // ADF is cleared by writing it as zero.  Writing one preserves an already
    // set flag but cannot set it in software (firmware preserves ADF in one
    // write and expects the following start write to drop the stale flag).
    const u8 next = u8((v & 0x7F) | (adcsr_ & v & kAdf));
    adcsr_ = next;
    const bool running = (adcsr_ & kAdst) != 0;
    if (running && !was_running) {
      channel_ = u8(first_channel());
      start(now);
    } else if (!running && was_running) {
      stop();
    } else {
      reschedule();  // ADF cleared / mode changed: the next visible completion moves
    }
    update_request();
  } else if (off == kAdcr) {
    adcr_ = u8(v & kTrge);
  }
  // ADDRA-ADDRD are read-only.
}

}  // namespace h8500

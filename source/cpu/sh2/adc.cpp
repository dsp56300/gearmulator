#include "cpu/sh2/adc.hpp"

#include <algorithm>

namespace sh2 {

namespace {
constexpr u64 kNever = emu::Scheduler::kNever;
}  // namespace

// ===========================================================================
// AdcHighSpeed (SH7014, section 13)

AdcHighSpeed::AdcHighSpeed(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc)
    : sched_(sched), clock_(clock), intc_(intc) {}

AdcHighSpeed::~AdcHighSpeed() {
  if (event_) sched_.cancel(event_);
}

void AdcHighSpeed::map(emu::IoMux& mux) {
  mux.assign(kAdcsr, 2, this);  // ADCSR, ADCR (H'FFFF83E2-EF are reserved: unassigned)
  mux.assign(kAddr, 16, this);  // ADDRA-ADDRH
}

void AdcHighSpeed::reset() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  event_at_ = 0;
  addr_.fill(0);
  adcsr_ = 0;
  adcr_ = 0;
  adf_read_ = false;
  dma_clear_pending_ = false;
  phase_ = Phase::kIdle;
  first_ = false;
  suspended_ = false;
  next_at_ = 0;
  power_on_at_ = kNever;
  round_len_ = 0;
  pos_ = 0;
  held_.fill(0);
  buf_count_ = 0;
  now_ = 0;
  update_request();
}

// ---------------------------------------------------------------------------
// Time

void AdcHighSpeed::sync(u64 now) {
  if (now > now_) now_ = now;
  while (phase_ != Phase::kIdle && next_at_ <= now) step(next_at_);
  arm();
}

// Keep exactly one scheduler event, at next_at_, while a step is pending.
void AdcHighSpeed::arm() {
  if (phase_ == Phase::kIdle) {
    if (event_) { sched_.cancel(event_); event_ = 0; }
    return;
  }
  if (event_ && event_at_ == next_at_) return;
  if (event_) sched_.cancel(event_);
  event_at_ = next_at_;
  event_ = sched_.schedule(next_at_, &AdcHighSpeed::on_event, this);
}

void AdcHighSpeed::on_event(void* self, u64 /*when*/, u64 now) {
  auto* a = static_cast<AdcHighSpeed*>(self);
  a->event_ = 0;
  a->sync(now);
}

// ---------------------------------------------------------------------------
// Sequencing

// ADST set (software, trigger, or resumption after an ADF clear in scan mode).
void AdcHighSpeed::start_run(u64 at) {
  build_round();
  pos_ = 0;
  first_ = true;
  suspended_ = false;
  // 13.4.7: with PWR = 0 the analog circuit is powered together with ADST;
  // either way the first conversion waits for the 200-state power-up.
  if (!powered()) power_on_at_ = at;
  const u64 ready = std::max(at, power_on_at_ + kPowerUp);
  phase_ = Phase::kSampling;
  next_at_ = ready + kFirstHold[cks()];
}

void AdcHighSpeed::stop() {
  phase_ = Phase::kIdle;
  first_ = false;
  suspended_ = false;
  if (!(adcr_ & kPwr)) power_on_at_ = kNever;
  // 13.6.5 (analog error on the first conversion after a forced stop in
  // high-speed start mode) is not modelled.
}

// The channels converted in one round, in order (13.2.2, tables 13.4-13.6).
void AdcHighSpeed::build_round() {
  const unsigned ch = adcsr_ & kChMask;
  const unsigned bufe = adcr_ & kBufeMask;
  round_len_ = 0;
  auto push = [&](unsigned c) { round_[round_len_++] = u8(c); };
  if (!(adcsr_ & kGrp)) {
    // Select mode: the CH channel, or with buffers the buffer source(s) - one
    // stage per start (figure 13.7, table 13.4 "select group mode").
    if (bufe == 0) push(ch);
    else if (bufe == 2) { push(0); push(1); }
    else push(0);
    return;
  }
  // Group mode: AN0-ANn.  Simultaneous sampling works in pairs (CH0 ignored,
  // table 13.6), as does the two-group buffer operation (table 13.4).
  unsigned n = ch;
  if (dsmp() || (bufe == 2 && ch < 4)) n |= 1;
  if (bufe == 0) {
    for (unsigned k = 0; k <= n; ++k) push(k);
    return;
  }
  // Buffer registers: BUFE = 01 -> ADDRB; 10 -> ADDRC, ADDRD; 11 -> ADDRB-D.
  // A group reaching past the buffer registers skips their channels (table
  // 13.5); a group inside them converts the source once per stage (table 13.4).
  const unsigned buf_top = bufe == 1 ? 1 : 3;
  const bool beyond = n > buf_top;
  for (unsigned k = 0; k <= n; ++k) {
    const bool is_buffer = bufe == 1 ? k == 1 : bufe == 2 ? (k == 2 || k == 3) : (k >= 1 && k <= 3);
    if (!is_buffer) push(k);
    else if (!beyond) push(bufe == 2 ? (k & 1) : 0);
  }
}

// Table 13.4: stages before ADF in select mode with buffers (CH beyond the
// table is clamped to the buffer depth).
unsigned AdcHighSpeed::buf_target() const {
  const unsigned ch = adcsr_ & kChMask;
  switch (adcr_ & kBufeMask) {
    case 1: return std::min(ch, 1u) + 1;
    case 2: return std::min(ch >> 1, 1u) + 1;
    default: return std::min(ch, 3u) + 1;
  }
}

// The sampling instant of round_[pos]: the host value is latched now.
void AdcHighSpeed::latch(unsigned pos) {
  const unsigned ch = round_[pos];
  held_[pos & 1] = sampler_ ? u16(sampler_(ch) & 0x3FF) : inputs_[ch];
}

// Conversion result into the data register, shifting the buffers (13.4.5).
void AdcHighSpeed::store(unsigned channel, u16 v) {
  switch (adcr_ & kBufeMask) {
    case 1:
      if (channel == 0) { addr_[1] = addr_[0]; addr_[0] = v; return; }
      break;
    case 2:
      if (channel <= 1) { addr_[channel + 2] = addr_[channel]; addr_[channel] = v; return; }
      break;
    case 3:
      if (channel == 0) { addr_[3] = addr_[2]; addr_[2] = addr_[1]; addr_[1] = addr_[0]; addr_[0] = v; return; }
      break;
    default: break;
  }
  addr_[channel] = v;
}

void AdcHighSpeed::step(u64 at) {
  const unsigned k = cks();
  if (phase_ == Phase::kSampling) {
    // End of tSPL: hold the input (both channels of a pair with DSMP) and convert.
    latch(pos_);
    if (dsmp() && pos_ + 1 < round_len_) latch(pos_ + 1);
    phase_ = Phase::kConverting;
    next_at_ = at + (first_ ? kFirstEnd[k] - kFirstHold[k] : kNext[k]);
    first_ = false;
    return;
  }
  // Conversion end.
  store(round_[pos_], held_[pos_ & 1]);
  if (++pos_ < round_len_) next_channel(at);
  else round_done(at);
}

// Schedule the conversion of round_[pos_] right after a conversion ended at `at`.
void AdcHighSpeed::next_channel(u64 at) {
  const unsigned k = cks();
  if (dsmp()) {
    if (pos_ & 1) {  // second channel of the pair: already held
      phase_ = Phase::kConverting;
      next_at_ = at + kNext[k];
    } else {  // next pair: both inputs are sampled anew
      phase_ = Phase::kSampling;
      next_at_ = at + kSample[k];
    }
    return;
  }
  // Conversions in succession: the input was sampled during the previous
  // conversion and is held at its end; only tCP remains.
  latch(pos_);
  phase_ = Phase::kConverting;
  next_at_ = at + kNext[k];
}

void AdcHighSpeed::round_done(u64 at) {
  bool flag = true;
  if (!(adcsr_ & kGrp) && (adcr_ & kBufeMask)) {
    // Select mode with buffers: ADF after the table 13.4 number of stages.
    if (++buf_count_ < buf_target()) flag = false;
    else buf_count_ = 0;
  }
  if (flag) adcsr_ |= kAdf;
  if (!(adcr_ & kScan)) {
    adcsr_ &= u8(~kAdst);
    phase_ = Phase::kIdle;
    if (!(adcr_ & kPwr)) power_on_at_ = kNever;  // low power mode: analog off
  } else if (flag && (adcsr_ & kAdie)) {
    // 13.5: scan mode pauses when ADF is set with ADIE = 1, until ADF is cleared.
    phase_ = Phase::kIdle;
    suspended_ = true;
  } else {
    pos_ = 0;
    next_channel(at);
  }
  update_request();
}

void AdcHighSpeed::update_request() { intc_.set_request(IrqSrc::Adi, (adcsr_ & kAdf) && (adcsr_ & kAdie)); }

void AdcHighSpeed::clear_adf(u64 now) {
  adcsr_ &= u8(~kAdf);
  update_request();
  if (suspended_ && (adcsr_ & kAdst)) start_run(now);
}

// ---------------------------------------------------------------------------
// Host side

void AdcHighSpeed::set_input(unsigned channel, u16 value10) {
  sync(clock_.now());  // a hold instant already due latches the old value
  inputs_[channel & 7] = u16(value10 & 0x3FF);
}

void AdcHighSpeed::dmac_activated() {
  sync(clock_.now());
  dma_clear_pending_ = true;
}

void AdcHighSpeed::trigger() {
  const u64 now = clock_.now();
  sync(now);
  if ((adcr_ & kTrgsMask) != kTrgsMtu || (adcsr_ & kAdst)) return;
  adcsr_ |= kAdst;
  start_run(now);
  arm();
}

// ---------------------------------------------------------------------------
// Registers

u8 AdcHighSpeed::read8(u32 a) {
  const u64 now = clock_.now();
  sync(now);
  if (a == kAdcsr) {
    if (adcsr_ & kAdf) adf_read_ = true;
    return adcsr_;
  }
  if (a == kAdcr) return u8(adcr_ & 0x7F);  // bit 7 reserved, reads 0
  if (a >= kAddr && a < kAddr + 16) {
    const u16 r = addr_[(a - kAddr) >> 1];
    if (dma_clear_pending_) { dma_clear_pending_ = false; clear_adf(now); arm(); }
    return (a & 1) ? u8(r) : u8(r >> 8);
  }
  return 0xFF;
}

u16 AdcHighSpeed::read16(u32 a) {
  if (a >= kAddr && a < kAddr + 16) {
    const u64 now = clock_.now();
    sync(now);
    const u16 r = addr_[(a - kAddr) >> 1];
    if (dma_clear_pending_) { dma_clear_pending_ = false; clear_adf(now); arm(); }
    return r;
  }
  return Device::read16(a);  // ADCSR:ADCR as a word
}

void AdcHighSpeed::write8(u32 a, u8 v) {
  const u64 now = clock_.now();
  sync(now);
  if (a == kAdcsr) {
    const u8 old = adcsr_;
    u8 next = u8((old & kAdf) | (v & 0x7F));
    if (!(v & kAdf) && adf_read_) next &= u8(~kAdf);  // read 1 then write 0
    adf_read_ = false;
    adcsr_ = next;
    if ((next & kAdst) && !(old & kAdst)) start_run(now);
    else if (!(next & kAdst) && (old & kAdst)) stop();
    else if (suspended_ && !(next & kAdf)) start_run(now);  // 13.5: resume on ADF clear
    update_request();
  } else if (a == kAdcr) {
    const u8 old = adcr_;
    adcr_ = u8(v & 0x7F);
    if ((adcr_ & kPwr) && !(old & kPwr)) {
      if (!powered()) power_on_at_ = now;  // high-speed start mode: analog on now
    } else if (!(adcr_ & kPwr) && (old & kPwr) && !(adcsr_ & kAdst)) {
      power_on_at_ = kNever;  // switched off by software (while halted)
    }
    if (!(adcr_ & kBufeMask)) buf_count_ = 0;  // 13.4.5 "Resetting the Number of Buffer Operations"
  }
  // ADDRA-ADDRH are read-only.
  arm();
}

// ===========================================================================
// AdcMidSpeed (SH7016/SH7017, section 14)

AdcMidSpeed::AdcMidSpeed(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base, u32 adcsr, u32 adcr,
                         IrqSrc src)
    : base_(base), adcsr_addr_(adcsr ? adcsr : base + 8), adcr_addr_(adcr ? adcr : base + 9), src_(src), sched_(sched),
      clock_(clock), intc_(intc) {}

AdcMidSpeed::~AdcMidSpeed() {
  if (event_) sched_.cancel(event_);
}

void AdcMidSpeed::map(emu::IoMux& mux) {
  mux.assign(base_, 8, this);
  mux.assign(adcsr_addr_, 1, this);
  mux.assign(adcr_addr_, 1, this);
}

void AdcMidSpeed::reset() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  event_at_ = 0;
  addr_.fill(0);
  adcsr_ = 0;
  adcr_ = 0;
  temp_ = 0;
  adf_read_ = false;
  dma_clear_pending_ = false;
  phase_ = Phase::kIdle;
  first_ = false;
  next_at_ = 0;
  channel_ = 0;
  held_ = 0;
  now_ = 0;
  update_request();
}

// ---------------------------------------------------------------------------
// Time

void AdcMidSpeed::sync(u64 now) {
  if (now > now_) now_ = now;
  while (phase_ != Phase::kIdle && next_at_ <= now) step(next_at_);
  arm();
}

void AdcMidSpeed::arm() {
  if (phase_ == Phase::kIdle) {
    if (event_) { sched_.cancel(event_); event_ = 0; }
    return;
  }
  if (event_ && event_at_ == next_at_) return;
  if (event_) sched_.cancel(event_);
  event_at_ = next_at_;
  event_ = sched_.schedule(next_at_, &AdcMidSpeed::on_event, this);
}

void AdcMidSpeed::on_event(void* self, u64 /*when*/, u64 now) {
  auto* a = static_cast<AdcMidSpeed*>(self);
  a->event_ = 0;
  a->sync(now);
}

// ---------------------------------------------------------------------------
// Sequencing

void AdcMidSpeed::start_run(u64 at) {
  // Single mode: channel CH; scan mode: the first channel of the group (14.4.2).
  channel_ = (adcsr_ & kScan) ? (adcsr_ & 4) : (adcsr_ & kChMask);
  first_ = true;
  phase_ = Phase::kSampling;
  next_at_ = at + kFirstHold[cks()];
}

void AdcMidSpeed::stop() {
  phase_ = Phase::kIdle;
  first_ = false;
}

void AdcMidSpeed::step(u64 at) {
  const unsigned k = cks();
  if (phase_ == Phase::kSampling) {
    held_ = sampler_ ? u16(sampler_(channel_) & 0x3FF) : inputs_[channel_];
    phase_ = Phase::kConverting;
    next_at_ = at + (first_ ? kFirstEnd[k] - kFirstHold[k] : kNextEnd[k] - kNextHold[k]);
    first_ = false;
    return;
  }
  // Conversion end: 10 bits left-justified into the group's data register.
  addr_[channel_ & 3] = u16(held_ << 6);
  if (adcsr_ & kScan) {
    if ((channel_ & 3) >= (adcsr_ & 3)) {
      adcsr_ |= kAdf;  // all selected channels converted
      channel_ = adcsr_ & 4;
    } else {
      ++channel_;
    }
    phase_ = Phase::kSampling;
    next_at_ = at + kNextHold[k];
  } else {
    adcsr_ |= kAdf;
    adcsr_ &= u8(~kAdst);
    phase_ = Phase::kIdle;
  }
  update_request();
}

// Any register access after DMAC activation clears ADF (14.2.2).
void AdcMidSpeed::access() {
  if (!dma_clear_pending_) return;
  dma_clear_pending_ = false;
  adcsr_ &= u8(~kAdf);
  update_request();
}

void AdcMidSpeed::update_request() { intc_.set_request(src_, (adcsr_ & kAdf) && (adcsr_ & kAdie)); }

// ---------------------------------------------------------------------------
// Host side

void AdcMidSpeed::set_input(unsigned channel, u16 value10) {
  sync(clock_.now());  // a hold instant already due latches the old value
  inputs_[channel & 7] = u16(value10 & 0x3FF);
}

void AdcMidSpeed::dmac_activated() {
  sync(clock_.now());
  dma_clear_pending_ = true;
}

void AdcMidSpeed::trigger() {
  const u64 now = clock_.now();
  sync(now);
  if (!(adcr_ & kTrge) || (adcsr_ & kAdst)) return;
  adcsr_ |= kAdst;
  start_run(now);
  arm();
}

// ---------------------------------------------------------------------------
// Registers

u8 AdcMidSpeed::read8(u32 a) {
  sync(clock_.now());
  access();
  const u32 off = a - base_;
  if (off < 8) {
    // 14.3: the upper byte is read directly and latches the lower byte into TEMP.
    const u16 r = addr_[off >> 1];
    if ((off & 1) == 0) { temp_ = u8(r); return u8(r >> 8); }
    return temp_;
  }
  if (a == adcsr_addr_) {
    if (adcsr_ & kAdf) adf_read_ = true;
    return adcsr_;
  }
  if (a == adcr_addr_) return u8(adcr_ | 0x7F);  // bits 6-0 reserved, read 1
  return 0xFF;
}

u16 AdcMidSpeed::read16(u32 a) {
  const u32 off = a - base_;
  if (off < 8) {
    sync(clock_.now());
    access();
    const u16 r = addr_[off >> 1];
    temp_ = u8(r);
    return r;
  }
  return Device::read16(a);  // ADCSR:ADCR as a word
}

void AdcMidSpeed::write8(u32 a, u8 v) {
  const u64 now = clock_.now();
  sync(now);
  access();
  if (a == adcsr_addr_) {
    const u8 old = adcsr_;
    u8 next = u8((old & kAdf) | (v & 0x7F));
    if (!(v & kAdf) && adf_read_) next &= u8(~kAdf);  // read 1 then write 0
    adf_read_ = false;
    adcsr_ = next;
    if ((next & kAdst) && !(old & kAdst)) start_run(now);
    else if (!(next & kAdst) && (old & kAdst)) stop();
    update_request();
  } else if (a == adcr_addr_) {
    adcr_ = u8(v & kTrge);
  }
  // ADDRA-ADDRD are read-only.
  arm();
}

}  // namespace sh2

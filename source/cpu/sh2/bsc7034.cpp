#include "cpu/sh2/bsc7034.hpp"

namespace sh2 {

Bsc7034::Bsc7034(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, Bus& bus)
    : sched_(sched), clock_(clock), intc_(intc), bus_(bus) {
  reset();
}

Bsc7034::~Bsc7034() {
  if (event_) sched_.cancel(event_);
}

void Bsc7034::map(emu::IoMux& mux) { mux.assign(kBase, 0x14, this); }

void Bsc7034::reset() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  bcr_ = 0;
  wcr1_ = 0xFFFF;
  wcr2_ = 0xFFFF;
  wcr3_ = 0xF800;
  dcr_ = pcr_ = 0;
  rcr_ = rtcsr_ = rtcnt_ = 0;
  rtcor_ = 0xFF;
  cmf_read_ = false;
  tick_ = now_ = 0;
  install_classes();
  update_request();
}

// ---------------------------------------------------------------------------
// Area timing

void Bsc7034::install_classes() {
  const unsigned a02lw = ((wcr3_ >> 13) & 3) + 1, a6lw = ((wcr3_ >> 11) & 3) + 1;
  (void)a6lw;  // area 6 is the multiplexed I/O space: 4 states regardless
  for (unsigned area = 0; area < 8; ++area) {
    AccessClass k;
    k.width = 16;
    k.base = 1;
    k.external = true;
    k.cacheable = false;
    const bool rw = (wcr1_ >> (8 + area)) & 1;
    if (area == 0 || area == 2) k.wait = u8(a02lw);
    else if (area == 6) k.wait = 3;
    else k.wait = rw ? 1 : 0;
    bus_.set_class(kAreaClass[area], k);
  }
}

// ---------------------------------------------------------------------------
// Refresh timer

void Bsc7034::sync(u64 now) {
  if (now <= now_) return;
  now_ = now;
  if (!running()) return;
  const u64 cur = now >> shift();
  u64 elapsed = cur - tick_;
  tick_ = cur;
  while (elapsed) {
    const u64 to_match = u64(rtcor_) + 1 - rtcnt_;  // counts until the wrap after RTCOR
    if (to_match > elapsed) { rtcnt_ = u8(rtcnt_ + elapsed); break; }
    elapsed -= to_match;
    rtcnt_ = 0;
    rtcsr_ |= kCmf;
  }
  update_request();
}

void Bsc7034::reschedule() {
  if (event_) { sched_.cancel(event_); event_ = 0; }
  if (!running() || (rtcsr_ & kCmf)) return;  // a second match changes nothing visible
  const u64 to_match = u64(rtcor_) + 1 - rtcnt_;
  event_ = sched_.schedule((tick_ + to_match) << shift(), &Bsc7034::on_event, this);
}

void Bsc7034::on_event(void* self, u64 /*when*/, u64 now) {
  auto* b = static_cast<Bsc7034*>(self);
  b->event_ = 0;
  b->sync(now);
  b->reschedule();
}

// ---------------------------------------------------------------------------
// Registers

u16 Bsc7034::read16(u32 addr) {
  switch (addr) {
    case kBcr: return bcr_;
    case kWcr1: return wcr1_;
    case kWcr2: return wcr2_;
    case kWcr3: return wcr3_;
    case kDcr: return dcr_;
    case kPcr: return pcr_;
    case kRcr: return rcr_;
    case kRtcsr:
      sync(clock_.now());
      if (rtcsr_ & kCmf) cmf_read_ = true;
      return rtcsr_;
    case kRtcnt: sync(clock_.now()); return rtcnt_;
    case kRtcor: return rtcor_;
    default: return 0;
  }
}

u8 Bsc7034::read8(u32 addr) {
  const u16 w = read16(addr & ~1u);
  return u8((addr & 1) ? w : w >> 8);
}

void Bsc7034::write16(u32 addr, u16 value) {
  const u8 key = u8(value >> 8), data = u8(value);
  switch (addr) {
    case kBcr: bcr_ = value & 0xF800; return;
    case kWcr1: wcr1_ = u16(value | 0x00FD); install_classes(); return;
    case kWcr2: wcr2_ = value; return;
    case kWcr3: wcr3_ = value & 0xF800; install_classes(); return;
    case kDcr: dcr_ = value & 0xFF00; return;
    case kPcr: pcr_ = value & 0xF800; return;
    case kRcr: if (key == 0x5A) rcr_ = data & 0xF8; return;
    case kRtcsr: {
      if (key != 0xA5) return;
      const u64 now = clock_.now();
      sync(now);
      u8 next = u8((rtcsr_ & kCmf) | (data & (kCmie | kCksMask)));
      if (!(data & kCmf) && cmf_read_) next &= u8(~kCmf);
      cmf_read_ = false;
      const bool was_running = running();
      const unsigned old_shift = shift();
      rtcsr_ = next;
      if (running() && (!was_running || shift() != old_shift)) tick_ = now >> shift();
      update_request();
      reschedule();
      return;
    }
    case kRtcnt:
      if (key != 0x69) return;
      sync(clock_.now());
      rtcnt_ = data;
      reschedule();
      return;
    case kRtcor:
      if (key != 0x96) return;
      sync(clock_.now());
      rtcor_ = data;
      reschedule();
      return;
    default: return;
  }
}

void Bsc7034::write8(u32 addr, u8 value) {
  if (addr >= kRcr) return;  // key-protected registers: word writes only
  const u32 base = addr & ~1u;
  const u16 old = read16(base);
  write16(base, (addr & 1) ? u16((old & 0xFF00) | value) : u16((old & 0x00FF) | (u16(value) << 8)));
}

}  // namespace sh2

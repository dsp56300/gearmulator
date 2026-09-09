#include "cpu/sh2/bsc.hpp"

#include <algorithm>

namespace sh2 {

namespace {
// Writable / readable-as-written bits of each register (8.2.1-8.2.8).  Reserved
// bits read 0 except BCR1 bit 13, which reads 1.
constexpr u16 kBcr1Mask = 0x010F;   // IOE, A3SZ-A0SZ (bits 7-4: "do not write 1", read 0)
constexpr u16 kBcr1Fixed = 0x2000;  // bit 13 always reads 1
constexpr u16 kWcr2Mask = 0x003F;   // DDW1-0, DSW3-0
constexpr u16 kDcrMask = 0xFFB7;    // bits 6 and 3 reserved, read 0
constexpr u16 kRtcsrMask = 0x007F;  // CMF, CMIE, CKS2-0, RFSH, RMD
}  // namespace

Bsc::Bsc(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, Bus& bus, const ChipConfig& cfg)
    : sched_(sched), clock_(clock), intc_(intc), bus_(bus), cfg_(cfg) {
  // Install the power-on bus timing so a board gets manual-accurate external
  // access costs even before the first reset.
  install_classes();
}

Bsc::~Bsc() {
  if (event_) sched_.cancel(event_);
}

void Bsc::map(emu::IoMux& mux) {
  mux.assign(kBcr1, 8, this);  // BCR1, BCR2, WCR1, WCR2 (H'FFFF8628-9 is the flash RAMER, not ours)
  mux.assign(kDcr, 8, this);   // DCR, RTCSR, RTCNT, RTCOR
}

void Bsc::reset() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  bcr1_ = kBcr1Init;
  bcr2_ = kBcr2Init;
  wcr1_ = kWcr1Init;
  wcr2_ = kWcr2Init;
  dcr_ = 0;
  rtcsr_ = 0;
  cmf_read_ = false;
  rtcnt_ = 0;
  rtcor_ = 0;
  tick_ = 0;
  now_ = 0;
  refreshes_ = 0;
  update_requests();
  install_classes();
}

// ---------------------------------------------------------------------------
// Bus timing

unsigned Bsc::cs_width(unsigned area) const {
  area &= 3;
  // 8.2.1: A0SZ is effective only with the on-chip ROM enabled; otherwise the
  // CS0 bus size comes from the mode pins (MD0: mode 0 = 8-bit, mode 1 = 16-bit).
  if (area == 0 && !cfg_.rom_enabled()) return cfg_.cs0_16bit() ? 16 : 8;
  return ((bcr1_ >> area) & 1) ? 16 : 8;
}

void Bsc::install_classes() {
  for (unsigned area = 0; area < 4; ++area) {
    AccessClass k;
    k.width = u8(cs_width(area));
    k.base = kOrdinaryBase;
    // Software waits (8.3.2).  Not charged: the WAIT pin, the idle cycles
    // between accesses (IW/CW/DIW, 8.6) and the CS assert extension (SW,
    // 8.3.3), which depend on the previous/next bus cycle and on whether the
    // bus was already idle; a class describes one cycle in isolation.
    unsigned wait = cs_wait(area);
    // 8.5.1: multiplexed I/O accesses to CS3 are an ordinary access preceded
    // by the fixed address-output cycles Ta1-Ta4 (figure 8.17).  The A14-
    // selected width cannot vary within a class; A3SZ is used.
    if (area == 3 && multiplex_io()) wait += kMuxIoAddressCycles;
    k.wait = u8(std::min(wait, 255u));
    k.external = true;
    k.cacheable = true;
    bus_.set_class(Bus::kClsCs0 + area, k);
  }
  {
    // DRAM (8.4.2, 8.4.3): Tp Tr Tc1 Tc2 plus DWR waits; TPC and RCD each add
    // one precharge / row-address cycle.  RAS up mode is assumed (every access
    // reopens the row): high-speed page mode and RAS down mode (8.4.4) would
    // skip Tp/Tr for same-row accesses, which a per-class cost cannot express.
    // The read wait count is used for writes as well (DWW is exposed raw).
    AccessClass k;
    k.width = u8(dram_width());
    k.base = kOrdinaryBase;
    const unsigned wait = kDramOverhead + ((dcr_ & kTpc) ? 1 : 0) + ((dcr_ & kRcd) ? 1 : 0) + dram_read_wait();
    k.wait = u8(std::min(wait, 255u));
    k.external = true;
    k.cacheable = true;
    bus_.set_class(Bus::kClsDram, k);
  }
}

// ---------------------------------------------------------------------------
// Refresh timer (8.2.6-8.2.8)

// CKS2-0 (8.2.6): stopped, phi/2, phi/8, phi/32, phi/128, phi/512, phi/2048, phi/4096.
unsigned Bsc::refresh_clock_shift() const {
  static constexpr unsigned kShift[8] = {0, 1, 3, 5, 7, 9, 11, 12};
  return kShift[(rtcsr_ >> 2) & 7];
}

// Advance the lazy counter to `now`.  A compare match happens on the count-up
// that follows RTCNT == RTCOR: RTCNT clears, CMF is set, and with RFSH = 1 /
// RMD = 0 a CAS-before-RAS refresh request is generated (8.2.7).  The manual's
// note that an untouched RTCNT = RTCOR = 0 does not set CMF (8.2.6) falls out
// of this: a match needs a count-up, and the clock is stopped after reset.
void Bsc::sync(u64 now) {
  if (now <= now_) return;
  now_ = now;
  const unsigned shift = refresh_clock_shift();
  if (shift == 0) return;
  const u64 cur = now >> shift;
  if (cur <= tick_) return;
  u64 remaining = cur - tick_;
  const u64 to_match = u64(u8(rtcor_ - rtcnt_)) + 1;
  if (remaining < to_match) {
    rtcnt_ = u8(rtcnt_ + remaining);
    tick_ = cur;
    return;
  }
  // First match, then whole periods of RTCOR + 1 ticks.
  remaining -= to_match;
  const u64 period = u64(rtcor_) + 1;
  const u64 matches = 1 + remaining / period;
  rtcnt_ = u8(remaining % period);
  tick_ = cur;
  rtcsr_ |= kCmf;
  if ((rtcsr_ & (kRfsh | kRmd)) == kRfsh) refreshes_ += matches;
  update_requests();
}

void Bsc::update_requests() {
  // 8.2.8: the request is held while CMF and CMIE are both set.
  intc_.set_request(IrqSrc::Cmi, (rtcsr_ & kCmf) && (rtcsr_ & kCmie));
}

// One event at the next match that can raise CMI (CMIE set, CMF still clear).
void Bsc::reschedule() {
  if (event_) { sched_.cancel(event_); event_ = 0; }
  const unsigned shift = refresh_clock_shift();
  if (shift == 0 || !(rtcsr_ & kCmie) || (rtcsr_ & kCmf)) return;
  const u64 to_match = u64(u8(rtcor_ - rtcnt_)) + 1;
  event_ = sched_.schedule((tick_ + to_match) << shift, &Bsc::on_event, this);
}

void Bsc::on_event(void* self, u64 when, u64 /*now*/) {
  auto* b = static_cast<Bsc*>(self);
  b->event_ = 0;
  b->sync(when);
  b->reschedule();
}

// ---------------------------------------------------------------------------
// Registers

u16 Bsc::read_reg(u32 reg) {
  switch (reg) {
    case kBcr1: return bcr1_;
    case kBcr2: return bcr2_;
    case kWcr1: return wcr1_;
    case kWcr2: return wcr2_;
    case kDcr: return dcr_;
    case kRtcsr: sync(clock_.now()); return rtcsr_;
    case kRtcnt: sync(clock_.now()); return rtcnt_;
    case kRtcor: return rtcor_;
    default: return 0xFFFF;
  }
}

void Bsc::write_reg(u32 reg, u16 v) {
  switch (reg) {
    case kBcr1:
      bcr1_ = u16((v & kBcr1Mask) | kBcr1Fixed);
      install_classes();
      break;
    case kBcr2:
      bcr2_ = v;  // idle / CS extension settings: stored, not charged (see install_classes)
      break;
    case kWcr1:
      wcr1_ = v;
      install_classes();
      break;
    case kWcr2:
      wcr2_ = u16(v & kWcr2Mask);
      install_classes();  // no CPU-visible effect (DMA single address waits only, 8.2.4)
      break;
    case kDcr:
      dcr_ = u16(v & kDcrMask);
      install_classes();
      break;
    case kRtcsr: {
      sync(clock_.now());
      const unsigned old_shift = refresh_clock_shift();
      // CMF clears only by writing 0 after it was read as 1; writing 1 has no effect.
      u16 flag = u16(rtcsr_ & kCmf);
      if (flag && cmf_read_ && !(v & kCmf)) { flag = 0; cmf_read_ = false; }
      rtcsr_ = u16(flag | (v & kRtcsrMask & ~kCmf));
      // A new prescaler restarts the tick reckoning from now (the counter value is kept).
      if (refresh_clock_shift() != old_shift && refresh_clock_shift() != 0) tick_ = tick_of(now_);
      update_requests();
      reschedule();
      break;
    }
    case kRtcnt:
      sync(clock_.now());
      rtcnt_ = u8(v);
      reschedule();
      break;
    case kRtcor:
      sync(clock_.now());
      rtcor_ = u8(v);
      reschedule();
      break;
    default: break;
  }
}

u16 Bsc::read16(u32 addr) {
  const u16 v = read_reg(addr);
  if (addr == kRtcsr && (v & kCmf)) cmf_read_ = true;
  return v;
}

void Bsc::write16(u32 addr, u16 value) { write_reg(addr, value); }

u8 Bsc::read8(u32 addr) {
  const u32 reg = addr & ~1u;
  const u16 v = read_reg(reg);
  if (addr & 1) {
    if (reg == kRtcsr && (v & kCmf)) cmf_read_ = true;  // CMF lives in the low byte
    return u8(v);
  }
  return u8(v >> 8);
}

// A byte write updates one half of the 16-bit register; the other half keeps
// its current value (so a high-byte write to RTCSR rewrites CMF as 1 = no change).
void Bsc::write8(u32 addr, u8 value) {
  const u32 reg = addr & ~1u;
  const u16 cur = read_reg(reg);
  const u16 v = (addr & 1) ? u16((cur & 0xFF00) | value) : u16((cur & 0x00FF) | (u16(value) << 8));
  write_reg(reg, v);
}

}  // namespace sh2

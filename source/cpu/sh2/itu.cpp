#include "cpu/sh2/itu.hpp"

namespace sh2 {

namespace {
constexpr IrqSrc kSrcA[Itu::kChannels] = {IrqSrc::Tgi0a, IrqSrc::Tgi1a, IrqSrc::Tgi2a, IrqSrc::Tgi3a, IrqSrc::Tgi4a};
constexpr IrqSrc kSrcB[Itu::kChannels] = {IrqSrc::Tgi0b, IrqSrc::Tgi1b, IrqSrc::Tgi2b, IrqSrc::Tgi3b, IrqSrc::Tgi4b};
constexpr IrqSrc kSrcV[Itu::kChannels] = {IrqSrc::Tci0v, IrqSrc::Tci1v, IrqSrc::Tci2v, IrqSrc::Tci3v, IrqSrc::Tci4v};
constexpr u64 kNever = ~u64(0);

// Counts from v up to t (t == v: a full turn).
constexpr u64 up_to(u16 v, u16 t) { return t > v ? u64(t - v) : u64(0x10000) - v + t; }
}  // namespace

Itu::Itu(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc) : sched_(sched), clock_(clock), intc_(intc) {
  reset();
}

Itu::~Itu() {
  if (event_) sched_.cancel(event_);
}

void Itu::map(emu::IoMux& mux) { mux.assign(kBase, 0x40, this); }

void Itu::reset() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  tstr_ = tsnc_ = tmdr_ = tfcr_ = 0;
  tocr_ = 0xFF;
  for (Channel& c : ch_) c = Channel{};
  now_ = 0;
  for (unsigned ch = 0; ch < kChannels; ++ch) update_request(ch);
}

// ---------------------------------------------------------------------------
// Counting

// The tick after the count sits on the CCLR register's value goes to 0
// instead of +1; reaching GRA / GRB sets IMFA / IMFB, wrapping sets OVF.
void Itu::advance(unsigned ch, u64 ticks) {
  Channel& c = ch_[ch];
  const unsigned cclr = (c.tcr & kCclrMask) >> 5;
  const bool clr_a = cclr == 1, clr_b = cclr == 2;
  while (ticks) {
    const bool at_clear = (clr_a && c.tcnt == c.gra) || (clr_b && c.tcnt == c.grb);
    u64 d = at_clear ? 1 : u64(0x10000) - c.tcnt;  // the clear tick, else the wrap to 0
    if (!at_clear) {
      d = std::min(d, up_to(c.tcnt, c.gra));
      d = std::min(d, up_to(c.tcnt, c.grb));
    }
    if (d > ticks) {
      c.tcnt = u16(c.tcnt + ticks);
      break;
    }
    ticks -= d;
    if (at_clear) {
      c.tcnt = 0;
    } else {
      const u32 nv = c.tcnt + u32(d);
      if (nv >= 0x10000) c.tsr |= kOvf;
      c.tcnt = u16(nv);
    }
    if (c.tcnt == c.gra) c.tsr |= kImfa;
    if (c.tcnt == c.grb) c.tsr |= kImfb;
  }
}

u64 Itu::distance(unsigned ch, bool enabled_only) const {
  const Channel& c = ch_[ch];
  const unsigned cclr = (c.tcr & kCclrMask) >> 5;
  const bool clr_a = cclr == 1, clr_b = cclr == 2;
  // Walk the specials from the current count until one qualifies (at most a
  // few steps: match A, match B, clear, wrap).
  u16 v = c.tcnt;
  u64 total = 0;
  for (int guard = 0; guard < 8; ++guard) {
    const bool at_clear = (clr_a && v == c.gra) || (clr_b && v == c.grb);
    u64 d = at_clear ? 1 : u64(0x10000) - v;
    if (!at_clear) {
      d = std::min(d, up_to(v, c.gra));
      d = std::min(d, up_to(v, c.grb));
    }
    total += d;
    u16 nv;
    bool wrap = false;
    if (at_clear) nv = 0;
    else { const u32 n = v + u32(d); wrap = n >= 0x10000; nv = u16(n); }
    const bool a = nv == c.gra, b = nv == c.grb;
    if (!enabled_only) return total;
    if ((a && (c.tier & kImfa)) || (b && (c.tier & kImfb)) || (wrap && (c.tier & kOvf))) return total;
    v = nv;
  }
  return kNever;
}

void Itu::sync(u64 now) {
  if (now <= now_) return;
  now_ = now;
  for (unsigned ch = 0; ch < kChannels; ++ch) {
    if (!running(ch) || !internal(ch)) continue;
    Channel& c = ch_[ch];
    const u64 tick = tick_of(ch, now);
    if (tick > c.tick) {
      advance(ch, tick - c.tick);
      c.tick = tick;
      update_request(ch);
    }
  }
}

void Itu::step_once(unsigned ch) {
  advance(ch, 1);
  update_request(ch);
}

void Itu::start(unsigned ch, u64 now) { ch_[ch].tick = tick_of(ch, now); }

void Itu::update_request(unsigned ch) {
  const Channel& c = ch_[ch];
  intc_.set_request(kSrcA[ch], (c.tsr & kImfa) && (c.tier & kImfa));
  intc_.set_request(kSrcB[ch], (c.tsr & kImfb) && (c.tier & kImfb));
  intc_.set_request(kSrcV[ch], (c.tsr & kOvf) && (c.tier & kOvf));
}

// One event for the earliest enabled special of any running internal channel.
void Itu::reschedule() {
  if (event_) { sched_.cancel(event_); event_ = 0; }
  u64 best = kNever;
  for (unsigned ch = 0; ch < kChannels; ++ch) {
    if (!running(ch) || !internal(ch)) continue;
    const u64 d = distance(ch, true);
    if (d == kNever) continue;
    best = std::min(best, (ch_[ch].tick + d) << shift(ch));
  }
  if (best != kNever) event_ = sched_.schedule(best, &Itu::on_event, this);
}

void Itu::on_event(void* self, u64 /*when*/, u64 now) {
  auto* t = static_cast<Itu*>(self);
  t->event_ = 0;
  t->sync(now);
  t->reschedule();
}

void Itu::tclk(unsigned n, bool high) {
  if (n >= 4 || tclk_high_[n] == high) return;
  tclk_high_[n] = high;
  sync(clock_.now());
  for (unsigned ch = 0; ch < kChannels; ++ch) {
    const Channel& c = ch_[ch];
    if (!running(ch) || internal(ch) || (c.tcr & 3) != n) continue;
    const unsigned ckeg = (c.tcr & kCkegMask) >> 3;
    const bool count = ckeg >= 2 || (ckeg == 0 && high) || (ckeg == 1 && !high);
    if (count) step_once(ch);
  }
}

void Itu::dmac_clear(IrqSrc src) {
  for (unsigned ch = 0; ch < kChannels; ++ch) {
    if (kSrcA[ch] != src) continue;
    ch_[ch].tsr &= u8(~kImfa);
    update_request(ch);
  }
  reschedule();
}

// ---------------------------------------------------------------------------
// Registers

u8 Itu::read8(u32 addr) {
  sync(clock_.now());
  const u32 off = addr - kBase;
  switch (off) {
    case 0x00: return u8(tstr_ | 0xE0);
    case 0x01: return u8(tsnc_ | 0xE0);
    case 0x02: return tmdr_ | 0x80;
    case 0x03: return tfcr_ | 0xC0;
    case 0x31: return tocr_ | 0xFC;
    default: break;
  }
  for (unsigned ch = 0; ch < kChannels; ++ch) {
    if (off < kChannelOffset[ch] || off >= kChannelOffset[ch] + (ch >= 3 ? 14u : 10u)) continue;
    Channel& c = ch_[ch];
    const u32 r = off - kChannelOffset[ch];
    if (r >= kTcnt && ((r - kTcnt) & 1)) { const u16 w = read16(addr & ~1u); return u8(w); }  // low byte of a 16-bit register
    switch (r) {
      case kTcr: return c.tcr | 0x80;
      case kTior: return c.tior | 0x88;
      case kTier: return c.tier | 0xF8;
      case kTsr: c.tsr_read |= u8(c.tsr & kFlagMask); return c.tsr | 0xF8;
      default: return u8(read16(addr) >> 8);  // high byte of a 16-bit register
    }
  }
  return 0xFF;
}

u16 Itu::read16(u32 addr) {
  sync(clock_.now());
  const u32 off = addr - kBase;
  for (unsigned ch = 0; ch < kChannels; ++ch) {
    if (off < kChannelOffset[ch] || off >= kChannelOffset[ch] + (ch >= 3 ? 14u : 10u)) continue;
    const Channel& c = ch_[ch];
    switch (off - kChannelOffset[ch]) {
      case kTcnt: return c.tcnt;
      case kGra: return c.gra;
      case kGrb: return c.grb;
      case kBra: return c.bra;
      case kBrb: return c.brb;
      default: return u16((u16(read8(addr)) << 8) | read8(addr + 1));  // two control bytes
    }
  }
  if (off <= 0x03 || off == 0x31 || off == 0x30) return u16((u16(read8(addr)) << 8) | read8(addr + 1));
  return 0xFFFF;
}

void Itu::write8(u32 addr, u8 value) {
  const u64 now = clock_.now();
  sync(now);
  const u32 off = addr - kBase;
  switch (off) {
    case 0x00: {
      const u8 started = u8(value & ~tstr_ & 0x1F);
      tstr_ = value & 0x1F;
      for (unsigned ch = 0; ch < kChannels; ++ch)
        if ((started >> ch) & 1) start(ch, now);
      reschedule();
      return;
    }
    case 0x01: tsnc_ = value & 0x1F; return;
    case 0x02: tmdr_ = value & 0x7F; return;
    case 0x03: tfcr_ = value & 0x3F; return;
    case 0x31: tocr_ = value & 0x03; return;
    default: break;
  }
  for (unsigned ch = 0; ch < kChannels; ++ch) {
    if (off < kChannelOffset[ch] || off >= kChannelOffset[ch] + (ch >= 3 ? 14u : 10u)) continue;
    Channel& c = ch_[ch];
    switch (off - kChannelOffset[ch]) {
      case kTcr:
        c.tcr = value & 0x7F;
        c.tick = tick_of(ch, now);  // a new prescaler counts from the next boundary
        break;
      case kTior: c.tior = value & 0x77; break;
      case kTier: c.tier = value & kFlagMask; update_request(ch); break;
      case kTsr: {
        // Flags clear only where they were read as set first.
        const u8 clear = u8(c.tsr_read & ~value & kFlagMask);
        c.tsr &= u8(~clear);
        c.tsr_read &= u8(~clear);
        update_request(ch);
        break;
      }
      default: {
        const u32 base = kBase + kChannelOffset[ch] + ((off - kChannelOffset[ch] - kTcnt) & ~1u) + kTcnt;
        const u16 old = read16(base);
        write16(base, (addr & 1) ? u16((old & 0xFF00) | value) : u16((old & 0x00FF) | (u16(value) << 8)));
        return;
      }
    }
    reschedule();
    return;
  }
}

void Itu::write16(u32 addr, u16 value) {
  const u64 now = clock_.now();
  sync(now);
  const u32 off = addr - kBase;
  for (unsigned ch = 0; ch < kChannels; ++ch) {
    if (off < kChannelOffset[ch] || off >= kChannelOffset[ch] + (ch >= 3 ? 14u : 10u)) continue;
    Channel& c = ch_[ch];
    switch (off - kChannelOffset[ch]) {
      case kTcnt: c.tcnt = value; c.tick = tick_of(ch, now); break;
      case kGra: c.gra = value; break;
      case kGrb: c.grb = value; break;
      case kBra: c.bra = value; return;
      case kBrb: c.brb = value; return;
      default:  // two control bytes
        write8(addr, u8(value >> 8));
        write8(addr + 1, u8(value));
        return;
    }
    reschedule();
    return;
  }
  if (off <= 0x03 || off == 0x30) {
    write8(addr, u8(value >> 8));
    write8(addr + 1, u8(value));
  }
}

}  // namespace sh2

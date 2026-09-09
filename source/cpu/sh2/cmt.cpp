#include "cpu/sh2/cmt.hpp"

namespace sh2 {

Cmt::Cmt(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc)
    : sched_(sched), clock_(clock), intc_(intc) {
  ch_[0].index = 0;
  ch_[0].src = IrqSrc::Cmi0;
  ch_[1].index = 1;
  ch_[1].src = IrqSrc::Cmi1;
  reset();
}

Cmt::~Cmt() {
  for (Channel& c : ch_)
    if (c.event) sched_.cancel(c.event);
}

void Cmt::map(emu::IoMux& mux) { mux.assign(kBase, 14, this); }

void Cmt::reset() {
  cmstr_ = 0;
  for (Channel& c : ch_) {
    if (c.event) sched_.cancel(c.event);
    c.event = 0;
    c.cmcsr = 0;
    c.cmcnt = 0;
    c.cmcor = 0xFFFF;
    c.cmf_read = false;
    c.tick = 0;
    c.now = 0;
    update_request(c);
  }
}

// ---------------------------------------------------------------------------
// Counter model
//
// A channel's counter advances by one at every prescaler boundary
// (multiple of 2^shift states) while STR is set.  From value V the match
// happens after (CMCOR - V) mod 2^16 + 1 counts: the counter reaches CMCOR
// and the following count clears it and sets CMF (15.4.2).  A CMCNT written
// above CMCOR simply wraps through H'FFFF -> H'0000 without any flag and
// matches on the way up.

void Cmt::sync(Channel& c, u64 now) {
  if (now <= c.now) return;
  c.now = now;
  if (!running(c)) return;
  const u64 cur = tick_of(c, now);
  const u64 elapsed = cur - c.tick;
  const u64 to_match = u64(u16(c.cmcor - c.cmcnt)) + 1;
  if (to_match <= elapsed) {
    c.cmcsr |= kCmf;
    // Every further period ends in another match (CMF already set).
    c.cmcnt = u16((elapsed - to_match) % (u64(c.cmcor) + 1));
    update_request(c);
  } else {
    c.cmcnt = u16(c.cmcnt + elapsed);
  }
  c.tick = cur;
}

void Cmt::update_request(Channel& c) {
  intc_.set_request(c.src, (c.cmcsr & kCmf) && (c.cmcsr & kCmie));
}

// One event per channel, for the next match while it can raise CMI (CMIE set,
// CMF clear).  Everything else is derived from the clock on access.
void Cmt::reschedule(Channel& c) {
  if (c.event) { sched_.cancel(c.event); c.event = 0; }
  if (!running(c) || !(c.cmcsr & kCmie) || (c.cmcsr & kCmf)) return;
  const u64 to_match = u64(u16(c.cmcor - c.cmcnt)) + 1;
  c.event = sched_.schedule((c.tick + to_match) << period_shift(c), c.index ? &Cmt::on_event1 : &Cmt::on_event0, this);
}

void Cmt::on_event0(void* self, u64 /*when*/, u64 now) {
  auto* t = static_cast<Cmt*>(self);
  t->ch_[0].event = 0;
  t->sync(t->ch_[0], now);
  t->reschedule(t->ch_[0]);
}

void Cmt::on_event1(void* self, u64 /*when*/, u64 now) {
  auto* t = static_cast<Cmt*>(self);
  t->ch_[1].event = 0;
  t->sync(t->ch_[1], now);
  t->reschedule(t->ch_[1]);
}

// ---------------------------------------------------------------------------
// Register writes
//
// Contention (15.5): the bus cycle is not modelled state by state.  A write
// lands at the state of the access after the counter has been synced to it,
// so a count falling on the same state is applied first and then overwritten
// by the write - the write wins, as in 15.5.2 / 15.5.3 (a byte write leaves
// the other byte as it was before the write).  The one difference from the
// hardware is 15.5.1: a compare match in the T2 state of a CMCNT write clears
// the counter and drops the write on the chip; here the match (and CMF) is
// applied and the written value then replaces the H'0000.

void Cmt::write_cmstr(u16 value) {
  const u64 now = clock_.now();
  for (Channel& c : ch_) sync(c, now);
  const u16 old = cmstr_;
  cmstr_ = u16(value & (kStr0 | kStr1));
  for (Channel& c : ch_) {
    // Starting a halted channel: it counts at the next prescaler boundary.
    if (running(c) && !((old >> c.index) & 1)) c.tick = tick_of(c, c.now);
    reschedule(c);
  }
}

void Cmt::write_cmcsr(Channel& c, u16 value, u16 mask) {
  sync(c, clock_.now());
  u16 next = c.cmcsr;
  if (mask & 0x00FF) {
    // CMF: writing 0 after having read 1 clears it; it can never be written 1.
    u16 flag = c.cmcsr & kCmf;
    if (!(value & kCmf) && c.cmf_read) flag = 0;
    c.cmf_read = false;
    const unsigned old_shift = period_shift(c);
    next = u16(flag | (value & (kCmie | kCksMask)));
    c.cmcsr = next;
    // A new divider: the count continues at the boundaries of the new clock.
    if (period_shift(c) != old_shift) c.tick = tick_of(c, c.now);
  }
  // Bits 15-8 are reserved: nothing to store.
  update_request(c);
  reschedule(c);
}

void Cmt::write_cmcnt(Channel& c, u16 value, u16 mask) {
  sync(c, clock_.now());
  c.cmcnt = u16((c.cmcnt & ~mask) | (value & mask));
  reschedule(c);
}

// ---------------------------------------------------------------------------
// Bus interface (8-, 16- and, through the Device default, 32-bit)

u16 Cmt::read16(u32 addr) {
  switch (addr) {
    case kCmstr: return cmstr_;
    case kCmcsr0:
    case kCmcsr1: {
      Channel& c = ch_[addr == kCmcsr1];
      sync(c, clock_.now());
      if (c.cmcsr & kCmf) c.cmf_read = true;
      return c.cmcsr;
    }
    case kCmcnt0:
    case kCmcnt1: {
      Channel& c = ch_[addr == kCmcnt1];
      sync(c, clock_.now());
      return c.cmcnt;
    }
    case kCmcor0: return ch_[0].cmcor;
    case kCmcor1: return ch_[1].cmcor;
    default: return 0xFFFF;  // odd address: not reachable through the mux
  }
}

u8 Cmt::read8(u32 addr) {
  const u16 v = read16(addr & ~1u);
  return u8((addr & 1) ? v : v >> 8);
}

void Cmt::write16(u32 addr, u16 value) {
  switch (addr) {
    case kCmstr: write_cmstr(value); break;
    case kCmcsr0: write_cmcsr(ch_[0], value, 0xFFFF); break;
    case kCmcsr1: write_cmcsr(ch_[1], value, 0xFFFF); break;
    case kCmcnt0: write_cmcnt(ch_[0], value, 0xFFFF); break;
    case kCmcnt1: write_cmcnt(ch_[1], value, 0xFFFF); break;
    case kCmcor0: sync(ch_[0], clock_.now()); ch_[0].cmcor = value; reschedule(ch_[0]); break;
    case kCmcor1: sync(ch_[1], clock_.now()); ch_[1].cmcor = value; reschedule(ch_[1]); break;
    default: break;
  }
}

void Cmt::write8(u32 addr, u8 value) {
  const u32 reg = addr & ~1u;
  const u16 mask = (addr & 1) ? 0x00FF : 0xFF00;
  const u16 v = u16((addr & 1) ? value : value << 8);
  switch (reg) {
    case kCmstr:
      if (addr & 1) write_cmstr(v);  // the high byte is all reserved
      break;
    case kCmcsr0: write_cmcsr(ch_[0], v, mask); break;
    case kCmcsr1: write_cmcsr(ch_[1], v, mask); break;
    case kCmcnt0: write_cmcnt(ch_[0], v, mask); break;
    case kCmcnt1: write_cmcnt(ch_[1], v, mask); break;
    case kCmcor0:
    case kCmcor1: {
      Channel& c = ch_[reg == kCmcor1];
      sync(c, clock_.now());
      c.cmcor = u16((c.cmcor & ~mask) | v);
      reschedule(c);
      break;
    }
    default: break;
  }
}

}  // namespace sh2

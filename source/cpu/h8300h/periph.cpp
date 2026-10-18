#include "cpu/h8300h/periph.hpp"

#include <bit>

#include <algorithm>
#include <cstdio>

namespace h8300h {

namespace {
constexpr u32 kItuBase = 0xFFFF60, kTpcBase = 0xFFFFA0, kWdtBase = 0xFFFFA8, kRfshBase = 0xFFFFAC;
constexpr u32 kSciBase[2] = {0xFFFFB0, 0xFFFFB8};
constexpr u32 kDmaBase[2] = {0xFFFF20, 0xFFFF30};
}  // namespace

Peripherals::Peripherals(emu::Scheduler& sched, Cpu& cpu, Bus& bus, emu::IoMux& io)
    : sched_(sched), cpu_(cpu), bus_(bus), io_(io) {
  io_.assign(0xFFFF1C, 0xE4, &sys_dev_);
  io_.assign(kDmaBase[0], 16, &dma_dev_[0]);
  io_.assign(kDmaBase[1], 16, &dma_dev_[1]);
  io_.assign(kItuBase, 64, &itu_dev_);
  io_.assign(kTpcBase, 8, &tpc_dev_);
  io_.assign(kWdtBase, 4, &wdt_dev_);
  io_.assign(kRfshBase, 4, &rfsh_dev_);
  io_.assign(kSciBase[0], 6, &sci_dev_[0]);
  io_.assign(kSciBase[1], 6, &sci_dev_[1]);
  cpu_.set_intc(this);
}

Peripherals::~Peripherals() {
  cpu_.set_intc(nullptr);
  for (auto& c : itu_) cancel(c.event);
  cancel(rfsh_event_);
  cancel(wdt_event_);
  for (auto& s : sci_) { cancel(s.tx_event); cancel(s.rx_event); }
}

void Peripherals::reset() {
  pending_ = level_ = 0;
  // system registers (manual appendix B)
  sys_.fill(0);
  sys_[kAbwcr & 0xFF] = (mode_ == 1 || mode_ == 3) ? 0xFF : 0x00;
  sys_[kAstcr & 0xFF] = 0xFF;
  sys_[kWcr & 0xFF] = 0xF3;
  sys_[kWcer & 0xFF] = 0xFF;
  sys_[kMdcr & 0xFF] = u8(0xC0 | (mode_ & 7));
  sys_[kSyscr & 0xFF] = 0x0B;
  sys_[kBrcr & 0xFF] = 0xFE;
  isr_read_mask_ = 0;
  apply_bus_controller();
  // ITU
  for (unsigned i = 0; i < 5; ++i) itu_reset_channel(i);
  tstr_ = tsnc_ = 0xE0;
  tmdr_ = 0x80;
  tfcr_ = 0xC0;
  toer_ = tocr_ = 0xFF;
  itu_space_.fill(0xFF);
  // TPC
  tpmr_ = 0xF0; tpcr_ = 0xFF; ndera_ = nderb_ = 0; tpc_output_ = 0;
  for (auto& v : tpc_next_) v = 0;
  tpc_notify();
  // refresh
  cancel(rfsh_event_);
  rfsh_[0] = 2; rfsh_[1] = 7; rfsh_[2] = 0; rfsh_[3] = 0xFF;
  rfsh_synced_ = now();
  rfsh_shift_ = 0;
  rfsh_cmie_ = rfsh_cmf_read_ = false;
  // watchdog
  cancel(wdt_event_);
  wdt_tcsr_ = 0x18; wdt_tcnt_ = 0; wdt_rstcsr_ = 0x3F;
  wdt_tcsr_read_ = wdt_rstcsr_read_ = false;
  wdt_pw_tcsr_ = wdt_pw_rst_ = 0;
  wdt_synced_ = now();
  // SCI
  for (unsigned ch = 0; ch < 2; ++ch) sci_reset(ch);
  // DMAC
  for (unsigned n = 0; n < 2; ++n) dma_reset(n);
  dreq_high_ = true;
  itu_update_levels();
  rfsh_update_level();
  wdt_update_level();
  update_irq();
}

void Peripherals::enter_software_standby() {
  // Modules that reset on standby (manual 20.4.2): the ITU, the SCIs, the
  // DMAC; the refresh controller and the watchdog keep parts of their state.
  for (unsigned i = 0; i < 5; ++i) itu_reset_channel(i);
  tstr_ = tsnc_ = 0xE0; tmdr_ = 0x80; tfcr_ = 0xC0; toer_ = tocr_ = 0xFF;
  itu_space_.fill(0xFF);
  itu_update_levels();
  rfsh_sync(now());
  rfsh_[1] &= 0x3F; rfsh_[2] = 0; rfsh_cmie_ = false; rfsh_cmf_read_ = false;
  rfsh_reschedule();
  rfsh_update_level();
  wdt_sync(now());
  wdt_tcsr_ = u8((wdt_tcsr_ & 7) | 0x18); wdt_tcnt_ = 0; wdt_tcsr_read_ = false;
  wdt_reschedule();
  wdt_update_level();
  for (unsigned ch = 0; ch < 2; ++ch) sci_reset(ch);
  for (unsigned n = 0; n < 2; ++n) dma_reset(n);
  update_irq();
}

// ---------------------------------------------------------------------------
// Interrupt controller (manual chapter 5)

int Peripherals::priority_bit(unsigned which) {
  if (which == 12) return 15;
  if (which == 13) return 14;
  if (which == 14 || which == 15) return 13;
  if (which == 16 || which == 17) return 12;
  if (which == 20 || which == 21) return 11;
  if (which >= 24 && which <= 26) return 10;
  if (which >= 28 && which <= 30) return 9;
  if (which >= 32 && which <= 34) return 8;
  if (which >= 36 && which <= 38) return 7;
  if (which >= 40 && which <= 42) return 6;
  if (which >= 44 && which <= 47) return 5;
  if (which >= 52 && which <= 55) return 3;
  if (which >= 56 && which <= 59) return 2;
  if (which == 60) return 1;
  return -1;
}

bool Peripherals::high_priority(unsigned which) const {
  const int bit = priority_bit(which);
  if (bit < 0) return false;
  const u16 ipr = u16((sys_[kIpra & 0xFF] << 8) | sys_[kIprb & 0xFF]);
  return (ipr & (1u << bit)) != 0;
}

bool Peripherals::acceptable(unsigned which, u8 ccr) const {
  if (which == kVecNmi) return true;
  if (which >= 12 && which < 18 && !(sys_[kIer & 0xFF] & (1u << (which - 12)))) return false;
  const bool ue = (sys_[kSyscr & 0xFF] & 0x08) != 0;
  if (ue) return (ccr & kCcrI) == 0;
  if (high_priority(which)) return (ccr & (kCcrI | kCcrUi)) != (kCcrI | kCcrUi);
  return (ccr & kCcrI) == 0;
}

bool Peripherals::arbitrate(unsigned& which, u8 ccr) const {
  u64 req = pending_ | level_;
  if (!req) return false;
  if ((req >> kVecNmi) & 1) { which = kVecNmi; return true; }
  if ((ccr & kCcrI) && ((sys_[kSyscr & 0xFF] & 0x08) || (ccr & kCcrUi))) return false;
  // Visit only asserted sources, preserving priority then vector ordering.
  req &= ((u64(1) << 61) - 1) & ~((u64(1) << 12) - 1);
  unsigned low = 0;
  while (req) {
    const unsigned w = std::countr_zero(req);
    req &= req - 1;
    if (!acceptable(w, ccr)) continue;
    if (high_priority(w)) { which = w; return true; }
    if (!low) low = w;
  }
  if (!low) return false;
  which = low;
  return true;
}

void Peripherals::acknowledge(unsigned which) { pending_ &= ~(u64(1) << which); }

bool Peripherals::standby_wake(u8 ccr) const {
  const u64 req = pending_ | level_;
  if ((req >> kVecNmi) & 1) return true;
  for (unsigned w = 12; w <= 14; ++w)
    if (((req >> w) & 1) && acceptable(w, ccr)) return true;
  return false;
}

void Peripherals::request(unsigned which) {
  if (which >= kVectorCount) return;
  if (dma_offer(which)) return;
  pending_ |= u64(1) << which;
  update_irq();
}

void Peripherals::clear_request(unsigned which) {
  if (which >= kVectorCount) return;
  pending_ &= ~(u64(1) << which);
  update_irq();
}

void Peripherals::set_level(unsigned which, bool asserted) {
  if (which >= kVectorCount) return;
  const u64 bit = u64(1) << which;
  const bool was = (level_ & bit) != 0;
  if (asserted && !was) {
    // A rising level is a request: the DMAC may take it instead of the CPU.
    if (dma_offer(which)) return;
    level_ |= bit;
    update_irq();
  } else if (!asserted && was) {
    level_ &= ~bit;
    update_irq();
  }
}

// ---------------------------------------------------------------------------
// System registers and bus controller

void Peripherals::apply_bus_controller() {
  const u8 abwcr = sys_[kAbwcr & 0xFF], astcr = sys_[kAstcr & 0xFF], wcr = sys_[kWcr & 0xFF], wcer = sys_[kWcer & 0xFF];
  for (unsigned area = 0; area < 8; ++area) {
    emu::AccessClass k;
    k.bus_bytes = (abwcr & (1u << area)) ? 1 : 2;
    k.states = (astcr & (1u << area)) ? 3 : 2;
    k.waits = (k.states == 3 && (wcer & (1u << area)) && ((wcr >> 2) & 3) == 0) ? u8(wcr & 3) : 0;
    k.external = true;
    bus_.set_class(kClsArea0 + area, k);
  }
  emu::AccessClass ram;
  ram.bus_bytes = 2; ram.states = 2; ram.waits = 0; ram.external = false;
  bus_.set_class(kClsIntRam, ram);
  emu::AccessClass regs;
  regs.bus_bytes = 1; regs.states = 3; regs.waits = 0; regs.external = false;
  bus_.set_class(kClsRegs, regs);
  emu::AccessClass unmapped;
  unmapped.bus_bytes = 2; unmapped.states = 3; unmapped.waits = 0; unmapped.external = true;
  bus_.set_class(15, unmapped);
}

u8 Peripherals::sys_read(u32 a) {
  const u8 reg = u8(a);
  if (reg == (kIsr & 0xFF)) {
    const u8 flags = u8(((pending_ | level_) >> 12) & 0x3F);
    isr_read_mask_ |= flags;
    return flags;
  }
  return sys_[reg];
}

void Peripherals::sys_write(u32 a, u8 v) {
  const u8 reg = u8(a);
  switch (reg) {
    case kMdcr & 0xFF: return;
    case kSyscr & 0xFF: sys_[reg] = u8(v | 2); update_irq(); return;
    case kBrcr & 0xFF: sys_[reg] = u8(v | 0x1E); return;
    case kIsr & 0xFF:
      // A flag clears only if it was read as set and then written zero.
      pending_ &= ~((u64(u8(~v)) & isr_read_mask_ & 0x3F) << 12);
      isr_read_mask_ = 0;
      update_irq();
      return;
    case kWcr & 0xFF: sys_[reg] = u8(v | 0xF0); break;
    case kIer & 0xFF: case kIpra & 0xFF: case kIprb & 0xFF: sys_[reg] = v; update_irq(); return;
    default: sys_[reg] = v; break;
  }
  // The bus retimes each access class that changed and tells the CPU which one.
  if (reg >= (kAbwcr & 0xFF) && reg <= (kWcer & 0xFF)) apply_bus_controller();
}

// ---------------------------------------------------------------------------
// 16-bit integrated timer unit (manual chapter 9).  Internal clocks count
// on the multiples of 2^prescale of the state clock; a channel's next
// event is its next compare match or overflow.

void Peripherals::itu_reset_channel(unsigned ch) {
  Channel& c = itu_[ch];
  cancel(c.event);
  c = Channel{};
  c.owner = this;
  c.index = ch;
  c.synced = now();
}

u8 Peripherals::itu_read(u32 a) {
  a -= kItuBase;
  switch (a) {
    case 0: return tstr_;
    case 1: return tsnc_;
    case 2: return tmdr_;
    case 3: return tfcr_;
    case 0x30: return toer_;
    case 0x31: return tocr_;
    default: break;
  }
  int ch = -1;
  if (a >= 0x4 && a < 0xE) { ch = 0; a -= 0x4; }
  else if (a >= 0xE && a < 0x18) { ch = 1; a -= 0xE; }
  else if (a >= 0x18 && a < 0x22) { ch = 2; a -= 0x18; }
  else if (a >= 0x22 && a < 0x30) { ch = 3; a -= 0x22; }
  else if (a >= 0x32 && a < 0x40) { ch = 4; a -= 0x32; }
  if (ch < 0) return itu_space_[a & 63];
  Channel& c = itu_[ch];
  switch (a) {
    case 0: return c.tcr;
    case 1: return c.tior;
    case 2: return c.tier;
    case 3: c.tsr_read_mask |= c.tsr & 7; return c.tsr;
    case 4: itu_sync(ch, now()); return u8(c.tcnt >> 8);
    case 5: itu_sync(ch, now()); return u8(c.tcnt);
    case 6: return u8(c.gra >> 8);
    case 7: return u8(c.gra);
    case 8: return u8(c.grb >> 8);
    case 9: return u8(c.grb);
    case 10: return u8(c.bra >> 8);
    case 11: return u8(c.bra);
    case 12: return u8(c.brb >> 8);
    default: return u8(c.brb);
  }
}

void Peripherals::itu_write(u32 a, u8 v) {
  a -= kItuBase;
  switch (a) {
    case 0: itu_sync_all(now()); tstr_ = u8(v | 0xE0); for (unsigned i = 0; i < 5; ++i) itu_reschedule(i); return;
    case 1: tsnc_ = u8(v | 0xE0); return;
    case 2: tmdr_ = u8(v | 0x80); return;
    case 3: tfcr_ = u8(v | 0xC0); return;
    case 0x30: toer_ = u8(v | 0xC0); return;
    case 0x31: tocr_ = u8(v | 0xEC); return;
    default: break;
  }
  int ch = -1;
  if (a >= 0x4 && a < 0xE) { ch = 0; a -= 0x4; }
  else if (a >= 0xE && a < 0x18) { ch = 1; a -= 0xE; }
  else if (a >= 0x18 && a < 0x22) { ch = 2; a -= 0x18; }
  else if (a >= 0x22 && a < 0x30) { ch = 3; a -= 0x22; }
  else if (a >= 0x32 && a < 0x40) { ch = 4; a -= 0x32; }
  if (ch < 0) { itu_space_[a & 63] = v; return; }
  Channel& c = itu_[ch];
  itu_sync(ch, now());
  switch (a) {
    case 0: c.tcr = u8(v | 0x80); break;
    case 1: c.tior = v; break;
    case 2: c.tier = u8(v | 0xF8); itu_update_levels(); break;
    case 3:
      c.tsr = u8(0xF8 | ((c.tsr & 7) & (v | u8(~c.tsr_read_mask))));
      c.tsr_read_mask = 0;
      itu_update_levels();
      break;
    case 4: c.tcnt = u16((c.tcnt & 0xFF) | (v << 8)); break;
    case 5: c.tcnt = u16((c.tcnt & 0xFF00) | v); break;
    case 6: c.gra = u16((c.gra & 0xFF) | (v << 8)); break;
    case 7: c.gra = u16((c.gra & 0xFF00) | v); break;
    case 8: c.grb = u16((c.grb & 0xFF) | (v << 8)); break;
    case 9: c.grb = u16((c.grb & 0xFF00) | v); break;
    case 10: c.bra = u16((c.bra & 0xFF) | (v << 8)); break;
    case 11: c.bra = u16((c.bra & 0xFF00) | v); break;
    case 12: c.brb = u16((c.brb & 0xFF) | (v << 8)); break;
    default: c.brb = u16((c.brb & 0xFF00) | v); break;
  }
  itu_reschedule(ch);
}

// One count clock of channel ch: the overflow and compare-match flags, the
// TPC triggers and the counter clearing.
void Peripherals::itu_clock(unsigned ch) {
  Channel& c = itu_[ch];
  u16 next = u16(c.tcnt + 1);
  if (next == 0) c.tsr |= 4;
  // General registers configured for input capture are latches, not compare targets.
  const bool match_a = (c.tior & 7) < 4 && next == c.gra;
  const bool match_b = ((c.tior >> 4) & 7) < 4 && next == c.grb;
  if (match_a) { c.tsr |= 1; tpc_compare(ch, false); }
  if (match_b) { c.tsr |= 2; tpc_compare(ch, true); }
  if ((match_a && (c.tcr & 0x60) == 0x20) || (match_b && (c.tcr & 0x60) == 0x40)) next = 0;
  c.tcnt = next;
}

void Peripherals::itu_sync(unsigned ch, u64 t) {
  Channel& c = itu_[ch];
  if (t < c.synced) c.synced = t;
  if (!itu_running(ch)) { c.synced = t; return; }
  const unsigned shift = c.tcr & 3;
  u64 clocks = (t >> shift) - (c.synced >> shift);
  c.synced = t;
  bool changed = clocks != 0;
  while (clocks) {
    // Bulk-advance to the next event (overflow or match), one clock at a time
    // only across it.
    const u16 tcnt = c.tcnt;
    u32 d = 0x10000u - tcnt;  // clocks to overflow
    if ((c.tior & 7) < 4) d = std::min(d, ((u32(c.gra) - tcnt - 1) & 0xFFFF) + 1);
    if (((c.tior >> 4) & 7) < 4) d = std::min(d, ((u32(c.grb) - tcnt - 1) & 0xFFFF) + 1);
    if (d > clocks) { c.tcnt = u16(tcnt + clocks); break; }
    c.tcnt = u16(tcnt + d - 1);
    itu_clock(ch);
    clocks -= d;
  }
  if (changed) itu_update_levels();
}

void Peripherals::itu_reschedule(unsigned ch) {
  Channel& c = itu_[ch];
  cancel(c.event);
  if (!itu_running(ch)) return;
  const unsigned shift = c.tcr & 3;
  const u16 tcnt = c.tcnt;
  u32 d = 0x10000u - tcnt;
  if ((c.tior & 7) < 4) d = std::min(d, ((u32(c.gra) - tcnt - 1) & 0xFFFF) + 1);
  if (((c.tior >> 4) & 7) < 4) d = std::min(d, ((u32(c.grb) - tcnt - 1) & 0xFFFF) + 1);
  // The d-th clock edge after the synchronisation point.
  const u64 when = ((c.synced >> shift) + d) << shift;
  c.event = sched_.schedule(when, &on_itu, &c);
}

void Peripherals::on_itu(void* channel, u64, u64) {
  auto* c = static_cast<Channel*>(channel);
  Peripherals* p = c->owner;
  c->event = 0;
  p->itu_sync(c->index, p->now());
  p->itu_reschedule(c->index);
}

void Peripherals::itu_update_levels() {
  u64 asserted = 0;
  for (unsigned i = 0; i < 5; ++i) {
    const Channel& c = itu_[i];
    asserted |= u64(c.tsr & c.tier & 7) << (24 + i * 4);
  }
  // Unchanged levels do nothing. On an edge retain the original per-source
  // reads: DMA may itself write timer registers while accepting a request.
  constexpr u64 mask = u64(0x77777) << 24;
  if (((asserted ^ level_) & mask) == 0) return;
  for (unsigned i = 0; i < 5; ++i) {
    const Channel& c = itu_[i];
    set_level(24 + i * 4, (c.tsr & c.tier & 1) != 0);
    set_level(25 + i * 4, (c.tsr & c.tier & 2) != 0);
    set_level(26 + i * 4, (c.tsr & c.tier & 4) != 0);
  }
}

void Peripherals::tick_extclock(unsigned ch) {
  if (ch >= 5 || !(tstr_ & (1u << ch))) return;
  itu_clock(ch);
  itu_update_levels();
}

void Peripherals::capture_a_edge(unsigned ch, bool high) {
  if (ch >= itu_.size()) return;
  const auto edge = itu_[ch].tior & 7;
  if (edge >= 6 || (edge == 4 && high) || (edge == 5 && !high)) capture_a(ch);
}

void Peripherals::capture_a(unsigned ch) {
  if (ch >= 5 || !(tstr_ & (1u << ch))) return;
  Channel& c = itu_[ch];
  if ((c.tior & 7) < 4) return;
  itu_sync(ch, now());
  c.gra = c.tcnt;
  c.tsr |= 1;
  if ((c.tcr & 0x60) == 0x20) c.tcnt = 0;
  itu_reschedule(ch);
  itu_update_levels();
}

u16 Peripherals::tcnt(unsigned ch) {
  if (ch >= 5) return 0;
  itu_sync(ch, now());
  return itu_[ch].tcnt;
}

// ---------------------------------------------------------------------------
// Programmable timing pattern controller (manual chapter 10)

u8 Peripherals::tpc_read(u32 a) {
  auto pair = [this](int low, int high) -> u8 {
    if (tpc_same_trigger(low)) return u8((tpc_next_[high] << 4) | tpc_next_[low]);
    return u8((tpc_next_[high] << 4) | 0x0F);
  };
  auto split = [this](int low) -> u8 { return tpc_same_trigger(low) ? u8(0xFF) : u8(0xF0 | tpc_next_[low]); };
  switch (a & 7) {
    case 0: return tpmr_;
    case 1: return tpcr_;
    case 2: return nderb_;
    case 3: return ndera_;
    case 4: return pair(2, 3);
    case 5: return pair(0, 1);
    case 6: return split(2);
    default: return split(0);
  }
}

void Peripherals::tpc_write(u32 a, u8 v) {
  auto pair = [this](int low, int high, u8 value) {
    tpc_next_[high] = value >> 4;
    if (tpc_same_trigger(low)) tpc_next_[low] = value & 0xF;
  };
  auto split = [this](int low, u8 value) { if (!tpc_same_trigger(low)) tpc_next_[low] = value & 0xF; };
  switch (a & 7) {
    case 0: tpmr_ = u8(v | 0xF0); break;
    case 1: tpcr_ = v; break;
    case 2: nderb_ = v; break;
    case 3: ndera_ = v; break;
    case 4: pair(2, 3, v); break;
    case 5: pair(0, 1, v); break;
    case 6: split(2, v); break;
    default: split(0, v); break;
  }
}

void Peripherals::tpc_compare(unsigned channel, bool b) {
  for (int group = 0; group < 4; ++group) {
    if (((tpcr_ >> (group * 2)) & 3) != int(channel)) continue;
    const bool non_overlap = (tpmr_ & (1u << group)) != 0;
    if (b && !non_overlap) continue;
    const u16 nibble = u16(0xFu << (group * 4));
    const u16 enabled = u16(((group < 2 ? ndera_ : nderb_) >> (group % 2 * 4) & 0xF) << (group * 4));
    if (!b) tpc_output_ = u16((tpc_output_ & ~enabled) | ((u16(tpc_next_[group]) << (group * 4)) & enabled));
    else tpc_output_ = u16(tpc_output_ & ~(enabled & nibble));
  }
  tpc_notify();
}

void Peripherals::tpc_notify() {
  const u16 enabled = u16((u16(nderb_) << 8) | ndera_);
  if (tpc_hook_) tpc_hook_(tpc_output_, enabled);
}

// ---------------------------------------------------------------------------
// Refresh controller (manual chapter 15), as an interval timer

u8 Peripherals::rfsh_read(u32 a) {
  a &= 3;
  if (a == 2) rfsh_sync(now());
  if (a == 1 && (rfsh_[1] & 0x80)) rfsh_cmf_read_ = true;
  return rfsh_[a];
}

void Peripherals::rfsh_write(u32 a, u8 v) {
  a &= 3;
  rfsh_sync(now());
  if (a == 0) {
    if (rfsh_[0] & 0x60) rfsh_[0] = u8((v & 0xE0) | (rfsh_[0] & 0x1D) | 2);
    else rfsh_[0] = u8(v | 2);
    if (rfsh_[0] & 0x60) {
      rfsh_[1] &= u8(~0x40);
      rfsh_cmie_ = false;
      rfsh_update_level();
    }
    rfsh_reschedule();
    return;
  }
  if (a == 1) {
    if (rfsh_[0] & 0x60) {
      const bool cmf = (rfsh_[1] & 0x80) && (!rfsh_cmf_read_ || (v & 0x80));
      rfsh_[1] = u8((rfsh_[1] & 0x38) | 7 | (cmf ? 0x80 : 0));
      rfsh_cmie_ = false;
      rfsh_cmf_read_ = false;
      rfsh_update_level();
      rfsh_reschedule();
      return;
    }
    static constexpr u32 shifts[8] = {0, 1, 3, 5, 7, 9, 11, 12};
    rfsh_shift_ = shifts[(v >> 3) & 7];
    rfsh_cmie_ = (v & 0x40) != 0;
    const bool cmf = (rfsh_[1] & 0x80) && (!rfsh_cmf_read_ || (v & 0x80));
    rfsh_[1] = u8((v & 0x78) | 7 | (cmf ? 0x80 : 0));
    rfsh_cmf_read_ = false;
    rfsh_update_level();
    rfsh_reschedule();
    return;
  }
  if (!(rfsh_[0] & 0x60)) rfsh_[a] = v;
  rfsh_reschedule();
}

void Peripherals::rfsh_sync(u64 t) {
  if (t < rfsh_synced_) rfsh_synced_ = t;
  if (!rfsh_shift_) { rfsh_synced_ = t; return; }
  u64 inc = (t >> rfsh_shift_) - (rfsh_synced_ >> rfsh_shift_);
  rfsh_synced_ = t;
  bool matched = false;
  while (inc) {
    const u32 d = ((u32(rfsh_[3]) - rfsh_[2] - 1) & 0xFF) + 1;  // clocks until RTCNT reaches RTCOR
    if (d > inc) { rfsh_[2] = u8(rfsh_[2] + inc); break; }
    rfsh_[2] = 0;
    rfsh_[1] |= 0x80;
    matched = true;
    inc -= d;
  }
  if (matched) rfsh_update_level();
}

void Peripherals::rfsh_reschedule() {
  cancel(rfsh_event_);
  if (!rfsh_shift_) return;
  const u32 d = ((u32(rfsh_[3]) - rfsh_[2] - 1) & 0xFF) + 1;
  const u64 when = ((rfsh_synced_ >> rfsh_shift_) + d) << rfsh_shift_;
  rfsh_event_ = sched_.schedule(when, &on_rfsh, this);
}

void Peripherals::on_rfsh(void* self, u64, u64) {
  auto* p = static_cast<Peripherals*>(self);
  p->rfsh_event_ = 0;
  p->rfsh_sync(p->now());
  p->rfsh_reschedule();
}

// ---------------------------------------------------------------------------
// Watchdog timer (manual chapter 11)

u8 Peripherals::wdt_read(u32 a) {
  switch (a & 3) {
    case 0: if (wdt_tcsr_ & 0x80) wdt_tcsr_read_ = true; return wdt_tcsr_;
    case 1: wdt_sync(now()); return wdt_tcnt_;
    case 3: if (wdt_rstcsr_ & 0x80) wdt_rstcsr_read_ = true; return wdt_rstcsr_;
    default: return 0xFF;
  }
}

void Peripherals::wdt_write(u32 a, u8 v) {
  wdt_sync(now());
  switch (a & 3) {
    case 0: wdt_pw_tcsr_ = v; break;
    case 1:
      if (wdt_pw_tcsr_ == 0x5A) wdt_tcnt_ = v;
      else if (wdt_pw_tcsr_ == 0xA5) {
        const bool overflow = (wdt_tcsr_ & 0x80) && (!wdt_tcsr_read_ || (v & 0x80));
        wdt_tcsr_ = u8((v & 0x67) | 0x18 | (overflow ? 0x80 : 0));
        if (!(wdt_tcsr_ & 0x20)) wdt_tcnt_ = 0;
        wdt_tcsr_read_ = false;
      }
      wdt_pw_tcsr_ = 0;
      break;
    case 2: wdt_pw_rst_ = v; break;
    default:
      if (wdt_pw_rst_ == 0xA5 && v == 0 && wdt_rstcsr_read_) wdt_rstcsr_ &= 0x7F;
      else if (wdt_pw_rst_ == 0x5A) wdt_rstcsr_ = u8((wdt_rstcsr_ & 0x80) | (v & 0x40) | 0x3F);
      wdt_pw_rst_ = 0;
      wdt_rstcsr_read_ = false;
      break;
  }
  wdt_reschedule();
  wdt_update_level();
}

void Peripherals::wdt_sync(u64 t) {
  if (t < wdt_synced_) wdt_synced_ = t;
  if (!(wdt_tcsr_ & 0x20)) { wdt_synced_ = t; return; }
  static constexpr unsigned shifts[8] = {1, 5, 6, 7, 8, 9, 11, 12};
  const unsigned shift = shifts[wdt_tcsr_ & 7];
  u64 inc = (t >> shift) - (wdt_synced_ >> shift);
  wdt_synced_ = t;
  while (inc) {
    const u32 d = 0x100u - wdt_tcnt_;
    if (d > inc) { wdt_tcnt_ = u8(wdt_tcnt_ + inc); break; }
    wdt_tcnt_ = 0;
    inc -= d;
    wdt_tcsr_ |= 0x80;
    if (wdt_tcsr_ & 0x40) {
      // Watchdog reset: the whole chip.
      wdt_rstcsr_ |= 0x80;
      if (reset_sink_) reset_sink_->watchdog_reset();
      return;
    }
  }
}

void Peripherals::wdt_reschedule() {
  cancel(wdt_event_);
  if (!(wdt_tcsr_ & 0x20)) return;
  static constexpr unsigned shifts[8] = {1, 5, 6, 7, 8, 9, 11, 12};
  const unsigned shift = shifts[wdt_tcsr_ & 7];
  const u32 d = 0x100u - wdt_tcnt_;
  const u64 when = ((wdt_synced_ >> shift) + d) << shift;
  wdt_event_ = sched_.schedule(when, &on_wdt, this);
}

void Peripherals::on_wdt(void* self, u64, u64) {
  auto* p = static_cast<Peripherals*>(self);
  p->wdt_event_ = 0;
  p->wdt_sync(p->now());
  p->wdt_reschedule();
  p->wdt_update_level();
}

// ---------------------------------------------------------------------------
// Serial communication interface (manual chapter 13), asynchronous and
// clocked modes at their programmed bit rate.

void Peripherals::sci_reset(unsigned ch) {
  Sci& s = sci_[ch];
  cancel(s.tx_event);
  cancel(s.rx_event);
  for (auto& v : s.data) v = 0;
  s.data[1] = 0xFF;
  s.scr = 0; s.tdr = 0xFF; s.rdr = 0; s.ssr = 0x84; s.ssr_read_mask = 0;
  s.tx_active = s.rx_active = false;
  s.queue.clear();
  sci_update_levels(ch);
}

// A bit takes 32 x 4^n x (N + 1) states asynchronous, 4 x 4^n x (N + 1) clocked synchronous: the manual's
// N = phi / (64 x 2^(2n-1) x B) - 1 and N = phi / (8 x 2^(2n-1) x B) - 1, n = SMR.CKS, N = BRR.  MIDI at 16 MHz is
// n = 0, N = 15.
u32 Peripherals::sci_frame_states(const Sci& s) const {
  const u8 smr = s.data[0];
  if (smr & 0x80) return u32(4u << (2 * (smr & 3))) * (u32(s.data[1]) + 1) * 8;
  const u32 bits = 1 + ((smr & 0x40) ? 7 : 8) + ((smr & 0x20) ? 1 : 0) + ((smr & 8) ? 2 : 1);
  const u32 divider = 32u << (2 * (smr & 3));
  return divider * (u32(s.data[1]) + 1) * bits;
}

void Peripherals::sci_update_levels(unsigned ch) {
  const Sci& s = sci_[ch];
  const unsigned base = ch ? 56 : 52;
  set_level(base + 0, (s.scr & 0x40) && (s.ssr & 0x38));
  set_level(base + 1, (s.scr & 0x40) && (s.ssr & 0x40));
  set_level(base + 2, (s.scr & 0x80) && (s.ssr & 0x80));
  set_level(base + 3, (s.scr & 0x04) && (s.ssr & 0x04));
}

// `at` is when the frame starts: the current time for a frame the firmware
// starts, the previous frame's nominal end for one that follows on (the bit
// clock runs continuously, whatever instruction the CPU was in).
void Peripherals::sci_start_tx(unsigned ch, u64 at) {
  Sci& s = sci_[ch];
  if (s.tx_active || !(s.scr & 0x20) || (s.ssr & 0x80)) return;
  s.tx_shift = s.tdr;
  s.tx_active = true;
  s.ssr = u8((s.ssr | 0x80) & ~0x04);
  s.tx_event = sched_.schedule(at + sci_frame_states(s), ch ? &on_sci_tx1 : &on_sci_tx0, this);
}

void Peripherals::sci_try_start_rx(unsigned ch, u64 at) {
  Sci& s = sci_[ch];
  if (s.rx_active || !(s.scr & 0x10) || s.queue.empty()) return;
  s.rx_shift = s.queue.front();
  s.queue.pop_front();
  s.rx_active = true;
  s.rx_event = sched_.schedule(at + sci_frame_states(s), ch ? &on_sci_rx1 : &on_sci_rx0, this);
}

void Peripherals::on_sci_tx(unsigned ch, u64 when) {
  Sci& s = sci_[ch];
  s.tx_event = 0;
  s.tx_active = false;
  if (s.tx_hook) s.tx_hook(s.tx_shift);
  if (!(s.ssr & 0x80)) sci_start_tx(ch, when);
  else s.ssr |= 4;
  sci_update_levels(ch);
}

void Peripherals::on_sci_rx(unsigned ch, u64 when) {
  Sci& s = sci_[ch];
  s.rx_event = 0;
  s.rx_active = false;
  if (s.ssr & 0x40) s.ssr |= 0x20;  // overrun
  else { s.rdr = s.rx_shift; s.ssr |= 0x40; }
  sci_update_levels(ch);
  sci_try_start_rx(ch, when);
}
void Peripherals::on_sci_tx0(void* self, u64 when, u64) { static_cast<Peripherals*>(self)->on_sci_tx(0, when); }
void Peripherals::on_sci_tx1(void* self, u64 when, u64) { static_cast<Peripherals*>(self)->on_sci_tx(1, when); }
void Peripherals::on_sci_rx0(void* self, u64 when, u64) { static_cast<Peripherals*>(self)->on_sci_rx(0, when); }
void Peripherals::on_sci_rx1(void* self, u64 when, u64) { static_cast<Peripherals*>(self)->on_sci_rx(1, when); }

u8 Peripherals::sci_read(unsigned ch, u32 a) {
  Sci& s = sci_[ch];
  switch (a & 7) {
    case 2: return s.scr;
    case 3: return s.tdr;
    case 4: s.ssr_read_mask |= s.ssr & 0xF8; return s.ssr;
    case 5: return s.rdr;
    default: return s.data[a & 7];
  }
}

void Peripherals::sci_write(unsigned ch, u32 a, u8 v) {
  Sci& s = sci_[ch];
  switch (a & 7) {
    case 2:
      if (!(v & 0x20)) { cancel(s.tx_event); s.tx_active = false; s.ssr |= 0x84; }
      if (!(v & 0x10)) { cancel(s.rx_event); s.rx_active = false; }
      s.scr = v;
      sci_start_tx(ch, now());
      sci_try_start_rx(ch, now());
      break;
    case 3:
      s.tdr = v;
      if (s.tdr_hook) s.tdr_hook(v);
      break;
    case 4: {
      const u8 old = s.ssr;
      const u8 kept = u8((old & 0xF8) & (v | u8(~s.ssr_read_mask)));
      s.ssr = u8(kept | (old & 0x06) | (v & 1));
      s.ssr_read_mask = 0;
      if ((old & 0x80) && !(s.ssr & 0x80)) sci_start_tx(ch, now());
      break;
    }
    case 5: break;
    default: s.data[a & 7] = v; break;
  }
  sci_update_levels(ch);
}

u8 Peripherals::sci_dma_read(unsigned ch, u32 a) {
  Sci& s = sci_[ch];
  if ((a & 7) != 5) return sci_read(ch, a);
  const u8 v = s.rdr;
  s.ssr &= u8(~0x40);
  sci_update_levels(ch);
  return v;
}

void Peripherals::sci_dma_write(unsigned ch, u32 a, u8 v) {
  Sci& s = sci_[ch];
  if ((a & 7) != 3) { sci_write(ch, a, v); return; }
  s.tdr = v;
  if (s.tdr_hook) s.tdr_hook(v);
  s.ssr &= u8(~0x80);
  sci_start_tx(ch, now());
  sci_update_levels(ch);
}

void Peripherals::receive(unsigned channel, const u8* data, size_t len) {
  if (channel > 1) return;
  Sci& s = sci_[channel];
  for (size_t i = 0; i < len; ++i) s.queue.push_back(data[i]);
  sci_try_start_rx(channel, now());
}

// ---------------------------------------------------------------------------
// DMA controller (manual chapter 8): short address mode (two subchannels A /
// B) and full address mode (normal and block transfers), activated by ITU
// compare matches, the SCI0 flags, DREQ or automatically.

void Peripherals::dma_reset(unsigned n) {
  Dma& d = dma_[n];
  for (auto& v : d.regs) v = 0;
  d.regs[0] = d.regs[8] = 0xFF;
  d.dtcr_read[0] = d.dtcr_read[1] = false;
}

u8 Peripherals::dma_read(unsigned n, u32 a) {
  Dma& d = dma_[n];
  a &= 15;
  if (a == 7 && !(d.regs[a] & 0x80)) d.dtcr_read[0] = true;
  if (a == 15 && !(d.regs[a] & 0x80)) d.dtcr_read[1] = true;
  return d.regs[a];
}

void Peripherals::dma_write(unsigned n, u32 a, u8 v) {
  Dma& d = dma_[n];
  a &= 15;
  if (a == 0 || a == 8) return;
  if (a == 7 || a == 15) {
    const int sub = a >> 3;
    if ((v & 0x80) && !(d.regs[a] & 0x80) && !d.dtcr_read[sub]) v &= 0x7F;
    d.dtcr_read[sub] = false;
  }
  d.regs[a] = v;
  if (a == 7 || a == 15) dma_attempt_auto(n);
}

int Peripherals::dma_source_vector(int selection) {
  if (selection < 4) return 24 + selection * 4;
  if (selection == 4) return 54;
  if (selection == 5) return 53;
  return -1;
}

bool Peripherals::dma_offer(unsigned which) {
  bool consumed = false;
  for (unsigned n = 0; n < 2; ++n) {
    Dma& d = dma_[n];
    if (which == kVecNmi) {
      if (dma_full_mode(d)) { d.regs[15] &= 0x7F; d.dtcr_read[1] = false; }
      continue;
    }
    if (dma_full_mode(d)) {
      const int source = d.regs[15] & 7;
      if (dma_full_enabled(d) && (d.regs[7] & 1) && source < 4 && dma_source_vector(source) == int(which)) {
        dma_transfer_full_block(n);
        consumed = true;
      }
      continue;
    }
    for (int sub = 0; sub < 2; ++sub)
      if ((d.regs[sub * 8 + 7] & 0x80) && dma_source_vector(d.regs[sub * 8 + 7] & 7) == int(which)) {
        dma_transfer_short(n, sub);
        consumed = true;
      }
  }
  return consumed;
}

void Peripherals::request_dreq(bool high) {
  const bool falling = dreq_high_ && !high;
  dreq_high_ = high;
  for (unsigned n = 0; n < 2; ++n) {
    Dma& d = dma_[n];
    if (dma_full_mode(d)) {
      if (!dma_full_enabled(d)) continue;
      const int source = d.regs[15] & 7;
      if ((source == 6 && falling) || (source == 7 && !high)) {
        if (d.regs[7] & 1) { if (source == 6 && falling) dma_transfer_full_block(n); }
        else dma_transfer_full_normal(n, false);
      }
      continue;
    }
    const u8 control = d.regs[15];
    if ((control & 0x80) && (((control & 7) == 6 && falling) || ((control & 7) == 7 && !high))) dma_transfer_short(n, 1);
  }
}

u32 Peripherals::dma_mar(const Dma& d, int sub) const {
  const int base = sub * 8;
  return (u32(d.regs[base + 1]) << 16) | (u32(d.regs[base + 2]) << 8) | d.regs[base + 3];
}
void Peripherals::dma_set_mar(Dma& d, int sub, u32 v) {
  const int base = sub * 8;
  v &= kAddrMask;
  d.regs[base + 1] = u8(v >> 16); d.regs[base + 2] = u8(v >> 8); d.regs[base + 3] = u8(v);
}
u16 Peripherals::dma_count(const Dma& d, int sub) const { const int base = sub * 8; return u16((d.regs[base + 4] << 8) | d.regs[base + 5]); }
void Peripherals::dma_set_count(Dma& d, int sub, u16 v) { const int base = sub * 8; d.regs[base + 4] = u8(v >> 8); d.regs[base + 5] = u8(v); }

u8 Peripherals::dma_read8(u32 a) {
  a &= kAddrMask;
  for (unsigned ch = 0; ch < 2; ++ch)
    if (a >= kSciBase[ch] && a < kSciBase[ch] + 8) return sci_dma_read(ch, a);
  return bus_.read8(a);
}
void Peripherals::dma_write8(u32 a, u8 v) {
  a &= kAddrMask;
  for (unsigned ch = 0; ch < 2; ++ch)
    if (a >= kSciBase[ch] && a < kSciBase[ch] + 8) { sci_dma_write(ch, a, v); return; }
  bus_.write8(a, v);
}

// The transfer uses the bus: the CPU waits for it.
void Peripherals::dma_transfer_value(u32 from, u32 to, int size) {
  u64 states = 0;
  if (size == 2) {
    states += cpu_.access_states(from & kAddrMask, 2) + cpu_.access_states(to & kAddrMask, 2);
    const u16 v = u16((dma_read8(from) << 8) | dma_read8(from + 1));
    dma_write8(to, u8(v >> 8));
    dma_write8(to + 1, u8(v));
  } else {
    states += cpu_.access_states(from & kAddrMask, 1) + cpu_.access_states(to & kAddrMask, 1);
    dma_write8(to, dma_read8(from));
  }
  cpu_.stall(states);
}

void Peripherals::dma_complete(unsigned n, int sub) {
  Dma& d = dma_[n];
  const int control = sub * 8 + 7;
  const bool interrupt_enabled = (d.regs[control] & 8) != 0;
  d.regs[control] &= 0x7F;
  if (interrupt_enabled) request(44 + n * 2 + sub);
}

void Peripherals::dma_transfer_short(unsigned n, int sub) {
  Dma& d = dma_[n];
  const int base = sub * 8;
  const u8 control = d.regs[base + 7];
  const int size = (control & 0x40) ? 2 : 1;
  const bool repeat = (control & 0x10) && !(control & 8);
  const bool idle = (control & 0x18) == 0x18;
  const bool receive = (control & 7) == 5;
  u32 memory = dma_mar(d, sub);
  const u32 io = 0xFFFF00u | d.regs[base + 6];
  dma_transfer_value(receive ? io : memory, receive ? memory : io, size);
  const int increment = idle ? 0 : ((control & 0x20) ? -size : size);
  memory = u32(s32(memory) + increment) & kAddrMask;
  dma_set_mar(d, sub, memory);
  if (repeat) {
    u8 remaining = u8(d.regs[base + 4] - 1);
    if (!remaining) {
      remaining = d.regs[base + 5];
      dma_set_mar(d, sub, u32(s32(memory) - increment * d.regs[base + 5]) & kAddrMask);
    }
    d.regs[base + 4] = remaining;
    return;
  }
  const u16 remaining = u16(dma_count(d, sub) - 1);
  dma_set_count(d, sub, remaining);
  if (!remaining) dma_complete(n, sub);
}

void Peripherals::dma_attempt_auto(unsigned n) {
  Dma& d = dma_[n];
  if (!dma_full_mode(d) || !dma_full_enabled(d) || (d.regs[7] & 1)) return;
  const int source = d.regs[15] & 7;
  if (source == 0 || source == 2) dma_transfer_full_normal(n, true);
}

void Peripherals::dma_transfer_full_normal(unsigned n, bool entire) {
  Dma& d = dma_[n];
  const u8 ca = d.regs[7], cb = d.regs[15];
  const int size = (ca & 0x40) ? 2 : 1;
  const int sm = (ca >> 4) & 3, dm = (cb >> 4) & 3;
  const int si = sm == 1 ? size : sm == 3 ? -size : 0;
  const int di = dm == 1 ? size : dm == 3 ? -size : 0;
  u32 from = dma_mar(d, 0), to = dma_mar(d, 1);
  u32 remaining = dma_count(d, 0);
  if (!remaining) remaining = 65536;
  const u32 transfers = entire ? remaining : 1;
  for (u32 i = 0; i < transfers; ++i) {
    dma_transfer_value(from, to, size);
    from = u32(s32(from) + si) & kAddrMask;
    to = u32(s32(to) + di) & kAddrMask;
  }
  remaining -= transfers;
  dma_set_mar(d, 0, from);
  dma_set_mar(d, 1, to);
  dma_set_count(d, 0, u16(remaining));
  if (!remaining) dma_complete(n, 0);
}

void Peripherals::dma_transfer_full_block(unsigned n) {
  Dma& d = dma_[n];
  const u8 ca = d.regs[7], cb = d.regs[15];
  const int size = (ca & 0x40) ? 2 : 1;
  const int sm = (ca >> 4) & 3, dm = (cb >> 4) & 3;
  const int si = sm == 1 ? size : sm == 3 ? -size : 0;
  const int di = dm == 1 ? size : dm == 3 ? -size : 0;
  u32 from = dma_mar(d, 0), to = dma_mar(d, 1);
  const u32 from0 = from, to0 = to;
  u32 block = d.regs[4];
  if (!block) block = 256;
  for (u32 i = 0; i < block; ++i) {
    dma_transfer_value(from, to, size);
    from = u32(s32(from) + si) & kAddrMask;
    to = u32(s32(to) + di) & kAddrMask;
  }
  d.regs[4] = d.regs[5];
  if (cb & 8) from = from0; else to = to0;
  dma_set_mar(d, 0, from);
  dma_set_mar(d, 1, to);
  const u16 blocks = u16(dma_count(d, 1) - 1);
  dma_set_count(d, 1, blocks);
  if (!blocks) dma_complete(n, 0);
}

}  // namespace h8300h

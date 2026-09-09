#include "cpu/sh2/mtu.hpp"

#include <algorithm>

namespace sh2 {

namespace {
constexpr u64 kNever = emu::Scheduler::kNever;
constexpr u32 kBases[3] = {Mtu::kBase0, Mtu::kBase1, Mtu::kBase2};
constexpr u32 kSpan[3] = {16, 12, 12};
// Channel 3/4 block (H'FFFF8200 + offset) -> {channel, canonical register
// offset}; 0xFF = not a channel register (stored in misc34_).
struct BlockReg { u8 ch, off; };
constexpr BlockReg kBlock34[0x30] = {
    {3, Mtu::kTcr}, {4, Mtu::kTcr}, {3, Mtu::kTmdr}, {4, Mtu::kTmdr},            // 00-03
    {3, Mtu::kTiorH}, {3, Mtu::kTiorL}, {4, Mtu::kTiorH}, {4, Mtu::kTiorL},      // 04-07
    {3, Mtu::kTier}, {4, Mtu::kTier}, {0xFF, 0}, {0xFF, 0},                       // 08-0B TOER, TOCR
    {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0},                                   // 0C-0F TGCR
    {3, Mtu::kTcnt}, {3, Mtu::kTcnt + 1}, {4, Mtu::kTcnt}, {4, Mtu::kTcnt + 1},  // 10-13
    {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0},                                   // 14-17 TCDR, TDDR
    {3, Mtu::kTgrA}, {3, Mtu::kTgrA + 1}, {3, Mtu::kTgrB}, {3, Mtu::kTgrB + 1},  // 18-1B
    {4, Mtu::kTgrA}, {4, Mtu::kTgrA + 1}, {4, Mtu::kTgrB}, {4, Mtu::kTgrB + 1},  // 1C-1F
    {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0},                                   // 20-23 TCNTS, TCBR
    {3, Mtu::kTgrC}, {3, Mtu::kTgrC + 1}, {3, Mtu::kTgrD}, {3, Mtu::kTgrD + 1},  // 24-27
    {4, Mtu::kTgrC}, {4, Mtu::kTgrC + 1}, {4, Mtu::kTgrD}, {4, Mtu::kTgrD + 1},  // 28-2B
    {3, Mtu::kTsr}, {4, Mtu::kTsr}, {0xFF, 0}, {0xFF, 0},                         // 2C-2F
};

constexpr IrqSrc kTgi[Mtu::kMaxChannels][4] = {
    {IrqSrc::Tgi0a, IrqSrc::Tgi0b, IrqSrc::Tgi0c, IrqSrc::Tgi0d},
    {IrqSrc::Tgi1a, IrqSrc::Tgi1b, IrqSrc::Tgi1b, IrqSrc::Tgi1b},  // C/D unused on channels 1, 2
    {IrqSrc::Tgi2a, IrqSrc::Tgi2b, IrqSrc::Tgi2b, IrqSrc::Tgi2b},
    {IrqSrc::Tgi3a, IrqSrc::Tgi3b, IrqSrc::Tgi3c, IrqSrc::Tgi3d},
    {IrqSrc::Tgi4a, IrqSrc::Tgi4b, IrqSrc::Tgi4c, IrqSrc::Tgi4d},
};
constexpr IrqSrc kTciv[Mtu::kMaxChannels] = {IrqSrc::Tci0v, IrqSrc::Tci1v, IrqSrc::Tci2v, IrqSrc::Tci3v, IrqSrc::Tci4v};
constexpr IrqSrc kTciu[Mtu::kMaxChannels] = {IrqSrc::Tci0v, IrqSrc::Tci1u, IrqSrc::Tci2u, IrqSrc::Tci3v, IrqSrc::Tci4v};  // [0], [3], [4] unused

// Phase counting direction for an edge on the A or B phase pin (tables
// 10.9-10.12).  `a`/`b` are the pin levels after the edge; returns +1 (count
// up), -1 (count down) or 0 (don't care).
int phase_direction(u8 mode, bool a_edge, bool a, bool b) {
  switch (mode) {
    case Mtu::kModePhase1:  // every edge counts: A edge up when A != B, B edge up when A == B
      return a_edge ? (a != b ? 1 : -1) : (a == b ? 1 : -1);
    case Mtu::kModePhase2:  // only falling edges of A count, direction from B
      return (a_edge && !a) ? (b ? 1 : -1) : 0;
    case Mtu::kModePhase3:  // A falling with B high: up; B falling with A high: down
      if (a_edge && !a && b) return 1;
      if (!a_edge && !b && a) return -1;
      return 0;
    case Mtu::kModePhase4:  // only B edges count
      return a_edge ? 0 : (a == b ? 1 : -1);
    default:
      return 0;
  }
}
}  // namespace

Mtu::Mtu(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, unsigned channels)
    : sched_(sched), clock_(clock), intc_(intc), channels_(channels) {
  reset();
}

Mtu::~Mtu() {
  if (event_) sched_.cancel(event_);
}

void Mtu::map(emu::IoMux& mux) {
  mux.assign(kTstr, 2, this);
  mux.assign(kBase0, 16, this);
  // Channels 1 and 2 have no TIORL: +3 stays unassigned ("do not access empty addresses").
  for (unsigned c = 1; c < 3; ++c) {
    mux.assign(kBases[c], 3, this);
    mux.assign(kBases[c] + kTier, 8, this);
  }
  if (channels_ > 3) mux.assign(kBase34, 0x30, this);
}

void Mtu::reset() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  for (Channel& ch : ch_) ch = Channel{};
  tstr_ = tsyr_ = 0;
  misc34_.fill(0);
  tclk_.fill(false);
  now_ = clock_.now();
  update_requests();
}

// ---------------------------------------------------------------------------
// Channel decoding

bool Mtu::internal_clock(unsigned c) const {
  if (!running(c) || phase_mode(c)) return false;
  const unsigned tpsc = ch_[c].tcr & kTpscMask;
  if (tpsc < 4) return true;
  if (c >= 3) return tpsc == 4 || tpsc == 5;                   // phi/256, phi/1024 (SH7040 table 12.4)
  return (c == 1 && tpsc == 6) || (c == 2 && tpsc == 7);  // phi/256, phi/1024 (table 10.4)
}

int Mtu::external_pin(unsigned c) const {
  if (phase_mode(c)) return -1;
  const unsigned tpsc = ch_[c].tcr & kTpscMask;
  if (tpsc < 4) return -1;
  switch (c) {
    case 0: return int(tpsc - 4);                 // TCLKA-D
    case 1: return tpsc <= 5 ? int(tpsc - 4) : -1;  // TCLKA, TCLKB (6 = phi/256, 7 = cascade)
    case 2: return tpsc <= 6 ? int(tpsc - 4) : -1;  // TCLKA-C (7 = phi/1024)
    default: return tpsc >= 6 ? int(tpsc - 6) : -1;  // channels 3, 4: TCLKA, TCLKB (4, 5 = phi/256, phi/1024)
  }
}

Mtu::Grid Mtu::grid(unsigned c) const {
  const unsigned tpsc = ch_[c].tcr & kTpscMask;
  unsigned shift = 0;
  switch (tpsc) {
    case 0: shift = 0; break;   // phi/1
    case 1: shift = 2; break;   // phi/4
    case 2: shift = 4; break;   // phi/16
    case 3: shift = 6; break;   // phi/64
    case 4: shift = 8; break;   // phi/256 (channels 3, 4)
    case 5: shift = 10; break;  // phi/1024 (channels 3, 4)
    case 6: shift = 8; break;   // phi/256 (channel 1)
    case 7: shift = 10; break;  // phi/1024 (channel 2)
    default: shift = 0; break;
  }
  // CKEG: effective for phi/4 and slower (10.2.1 note 2).  Falling edges lie
  // half a period after the rising ones; both edges double the rate.
  Grid g{shift, 0};
  if (shift > 0) {
    const unsigned ckeg = (ch_[c].tcr >> 3) & 3;
    if (ckeg & 2) g.shift = shift - 1;
    else if (ckeg & 1) g.offset = u64(1) << (shift - 1);
  }
  return g;
}

int Mtu::clear_source(unsigned c) const {
  switch ((ch_[c].tcr >> 5) & 7) {
    case 1: return 0;                   // TGRA
    case 2: return 1;                   // TGRB
    case 5: return four_tgr(c) ? 2 : -1;  // TGRC (channels 0, 3, 4)
    case 6: return four_tgr(c) ? 3 : -1;  // TGRD (channels 0, 3, 4)
    default: return -1;                 // disabled (0, 4) or synchronous clear (3, 7)
  }
}

// ---------------------------------------------------------------------------
// Counter model

u64 Mtu::ticks_to_next_special(unsigned c) const {
  // The transition off value v happens (v - tcnt) mod 2^16 + 1 ticks from now:
  // the count reaches v and the match / overflow is signalled on the following
  // count clock (figure 10.37).  Only output compare registers match; a TGR set
  // to input capture or acting as a buffer register never does.
  const Channel& ch = ch_[c];
  u64 best = u64(u16(0xFFFF - ch.tcnt)) + 1;  // overflow
  for (unsigned x = 0; x < ntgr(c); ++x)
    if (is_compare(c, x)) best = std::min(best, u64(u16(ch.tgr[x] - ch.tcnt)) + 1);
  return best;
}

u64 Mtu::next_special_time(unsigned c) const {
  if (!internal_clock(c)) return kNever;
  return time_of(c, ch_[c].tick + ticks_to_next_special(c));
}

void Mtu::set_flag(unsigned c, u8 flag, u64 at) {
  ch_[c].tsr |= flag;
  if (flag == kTgfa && (ch_[c].tier & kTtge) && adc_sink_) adc_sink_(c, at);  // 10.5.3
}

void Mtu::set_output(unsigned c, unsigned pin, bool level, u64 at) {
  if (ch_[c].out[pin] == level) return;
  ch_[c].out[pin] = level;
  if (out_sink_) out_sink_(c, pin, level, at);
}

void Mtu::clear_counter(unsigned c, u64 at) {
  ch_[c].tcnt = 0;
  // PWM mode 2: every output returns to its TIOR initial level on a counter
  // clear (10.4.6); the period register's pin therefore never moves.
  if (mode(c) == kModePwm2)
    for (unsigned x = 0; x < ntgr(c); ++x)
      if (is_compare(c, x) && (io(c, x) & 3)) set_output(c, x, (io(c, x) >> 2) & 1, at);
}

void Mtu::propagate_sync_clear(unsigned src, u64 at) {
  // A clear of a synchronised channel clears every other synchronised channel
  // whose CCLR selects "synchronous clear" (10.2.9, 10.4.3).
  for (unsigned j = 0; j < channels_; ++j)
    if (j != src && synced(j) && sync_clear_selected(j)) clear_counter(j, at);
}

// Apply the count clock that moves TCNT off its current value: compare match
// flags / outputs / buffer transfers, overflow or underflow, then the count or
// clear itself.  Returns true if the channel's own clear source fired.
bool Mtu::transition(unsigned c, bool up, bool sync_clear, u64 at) {
  Channel& ch = ch_[c];
  const unsigned n = ntgr(c);
  bool match[4] = {false, false, false, false};
  for (unsigned x = 0; x < n; ++x) match[x] = matches(c, x);
  const int cs = clear_source(c);
  const bool own = cs >= 0 && match[cs];
  const bool clear = own || sync_clear;
  // 10.7.11: a clear coinciding with overflow / underflow wins and the flag is not set.
  const bool overflow = up && ch.tcnt == 0xFFFF && !clear;
  const bool underflow = !up && ch.tcnt == 0x0000 && !clear;

  for (unsigned x = 0; x < n; ++x)
    if (match[x]) set_flag(c, u8(1u << x), at);
  if (overflow) set_flag(c, kTcfv, at);
  if (underflow) set_flag(c, kTcfu, at);

  auto apply = [&](unsigned code, unsigned pin) {
    switch (code & 3) {
      case 1: set_output(c, pin, false, at); break;
      case 2: set_output(c, pin, true, at); break;
      case 3: set_output(c, pin, !ch.out[pin], at); break;
      default: break;  // output disabled
    }
  };
  if (mode(c) == kModePwm1) {
    // TGRA/TGRB drive TIOCA, TGRC/TGRD drive TIOCC (table 10.7); equal values
    // (both matching at once) leave the output unchanged (10.4.6).
    for (unsigned p = 0; p + 1 < n; p += 2) {
      if (match[p] && match[p + 1]) continue;
      if (match[p]) apply(io(c, p), p);
      else if (match[p + 1]) apply(io(c, p + 1), p);
    }
  } else {
    for (unsigned x = 0; x < n; ++x)
      if (match[x]) apply(io(c, x), x);
  }

  // Buffer operation (4-TGR channels, 10.4.4): the buffer register is
  // transferred to the general register on its compare match (figure 10.41).
  if (four_tgr(c)) {
    if (match[0] && (ch.tmdr & kBfa)) ch.tgr[0] = ch.tgr[2];
    if (match[1] && (ch.tmdr & kBfb)) ch.tgr[1] = ch.tgr[3];
  }

  // Cascade connection (10.4.5): channel 1 counts on TCNT2 overflow / underflow.
  if (c == 2 && (overflow || underflow) && cascaded(1) && running(1)) step_external(1, up, at);

  if (clear) clear_counter(c, at);
  else ch.tcnt = up ? u16(ch.tcnt + 1) : u16(ch.tcnt - 1);
  ch.up = up;
  return own;
}

void Mtu::step_external(unsigned c, bool up, u64 at) {
  if (transition(c, up, false, at) && synced(c)) propagate_sync_clear(c, at);
}

// Bring every channel up to date with time `now`, replaying the specials of
// all channels in time order so that cross-channel effects (synchronous
// clear, cascade) land on counters that are exactly at the right state.
void Mtu::sync(u64 now) {
  if (now <= now_) return;
  for (;;) {
    u64 next[kMaxChannels];
    u64 t = kNever;
    for (unsigned c = 0; c < channels_; ++c) t = std::min(t, next[c] = next_special_time(c));
    if (t > now) break;
    // Every channel advances linearly to t; the ones whose special falls on t
    // stop on the special value and take their transition below.
    bool special[kMaxChannels] = {};
    for (unsigned c = 0; c < channels_; ++c) {
      if (!internal_clock(c)) continue;
      Channel& ch = ch_[c];
      const u64 cur = tick_of(c, t);
      special[c] = next[c] == t;
      ch.tcnt = u16(ch.tcnt + (cur - ch.tick) - (special[c] ? 1 : 0));
      ch.tick = cur;
    }
    // Synchronous clear: decided from the state before any transition so that
    // a channel clearing at t clears the others at t, whatever their order.
    bool sync_clear = false;
    for (unsigned c = 0; c < channels_; ++c)
      if (special[c] && synced(c) && own_clear(c)) sync_clear = true;
    for (unsigned c = 0; c < channels_; ++c) {
      const bool cleared_by_sync = sync_clear && synced(c) && sync_clear_selected(c);
      if (special[c]) transition(c, true, cleared_by_sync, t);
      else if (cleared_by_sync) clear_counter(c, t);
    }
  }
  for (unsigned c = 0; c < channels_; ++c) {
    if (!internal_clock(c)) continue;
    Channel& ch = ch_[c];
    const u64 cur = tick_of(c, now);
    ch.tcnt = u16(ch.tcnt + (cur - ch.tick));
    ch.tick = cur;
  }
  now_ = now;
  update_requests();
}

void Mtu::update_requests() {
  for (unsigned c = 0; c < channels_; ++c) {
    const Channel& ch = ch_[c];
    for (unsigned x = 0; x < ntgr(c); ++x)
      intc_.set_request(kTgi[c][x], ((ch.tsr >> x) & 1) && ((ch.tier >> x) & 1));
    intc_.set_request(kTciv[c], (ch.tsr & kTcfv) && (ch.tier & kTciev));
    if (c == 1 || c == 2) intc_.set_request(kTciu[c], (ch.tsr & kTcfu) && (ch.tier & kTcieu));  // TCFU: channels 1, 2 only
  }
}

// Earliest special of channel `c` that somebody can observe before the next
// register access: a compare match or overflow raising an enabled interrupt
// whose flag is still clear, an A/D trigger, anything at all when an output
// sink watches the pins, a clear that redirects a synchronised channel, or a
// TCNT2 overflow feeding a cascaded channel 1.  Flag-only specials are
// recovered lazily by sync().
u64 Mtu::next_interesting_time(unsigned c) const {
  if (!internal_clock(c)) return kNever;
  const Channel& ch = ch_[c];
  const bool all = bool(out_sink_);
  u64 best = kNever;
  auto consider = [&](u16 v, bool enabled) {
    if (enabled) best = std::min(best, u64(u16(v - ch.tcnt)) + 1);
  };
  bool clears_others = false;
  if (synced(c))
    for (unsigned j = 0; j < channels_; ++j)
      if (j != c && synced(j) && sync_clear_selected(j)) clears_others = true;
  const int cs = clear_source(c);
  for (unsigned x = 0; x < ntgr(c); ++x) {
    if (!is_compare(c, x)) continue;
    const bool irq = ((ch.tier >> x) & 1) && !((ch.tsr >> x) & 1);
    const bool adc = x == 0 && (ch.tier & kTtge) && adc_sink_;
    consider(ch.tgr[x], all || irq || adc || (clears_others && int(x) == cs));
  }
  bool ovf = all || ((ch.tier & kTciev) && !(ch.tsr & kTcfv));
  if (c == 2 && cascaded(1) && running(1)) ovf = true;
  consider(0xFFFF, ovf);
  return best == kNever ? kNever : time_of(c, ch.tick + best);
}

void Mtu::reschedule() {
  if (event_) { sched_.cancel(event_); event_ = 0; }
  u64 best = kNever;
  for (unsigned c = 0; c < channels_; ++c) best = std::min(best, next_interesting_time(c));
  if (best != kNever) event_ = sched_.schedule(best, &Mtu::on_event, this);
}

void Mtu::on_event(void* self, u64 /*when*/, u64 now) {
  auto* m = static_cast<Mtu*>(self);
  m->event_ = 0;
  m->sync(now);
  m->reschedule();
}

// ---------------------------------------------------------------------------
// Pins

void Mtu::set_tclk(unsigned pin, bool level) {
  if (pin > 3 || tclk_[pin] == level) return;
  sync(clock_.now());
  tclk_[pin] = level;
  for (unsigned c = 0; c < channels_; ++c) {
    if (!running(c)) continue;
    if (phase_mode(c)) {
      const unsigned a = c == 1 ? 0 : 2;  // table 10.8: channel 1 on TCLKA/B, channel 2 on TCLKC/D
      if (pin != a && pin != a + 1) continue;
      const int dir = phase_direction(mode(c), pin == a, tclk_[a], tclk_[a + 1]);
      if (dir) step_external(c, dir > 0, now_);
    } else if (external_pin(c) == int(pin)) {
      const unsigned ckeg = (ch_[c].tcr >> 3) & 3;  // 00 rising, 01 falling, 1x both
      if (ckeg >= 2 || (ckeg == 0) == level) step_external(c, true, now_);
    }
  }
  update_requests();
  reschedule();
}

void Mtu::capture(unsigned c, unsigned x, u64 at) {
  Channel& ch = ch_[c];
  // Input capture buffer operation (figure 10.42): the old TGR moves to the buffer.
  if (four_tgr(c) && x == 0 && (ch.tmdr & kBfa)) ch.tgr[2] = ch.tgr[0];
  if (four_tgr(c) && x == 1 && (ch.tmdr & kBfb)) ch.tgr[3] = ch.tgr[1];
  ch.tgr[x] = ch.tcnt;  // captures even when the flag is still set
  set_flag(c, u8(1u << x), at);
  if (clear_source(c) == int(x)) {
    clear_counter(c, at);
    if (synced(c)) propagate_sync_clear(c, at);
  }
}

void Mtu::capture_edge(unsigned c, unsigned pin, bool level) {
  if (c > 2 || pin >= ntgr(c)) return;
  Channel& ch = ch_[c];
  if (ch.in[pin] == level) return;
  ch.in[pin] = level;
  if (!is_capture(c, pin)) return;
  const u8 code = io(c, pin);
  // Codes 11xx on channels 0 and 1 select another channel's count clock or
  // compare match as the capture source (10.2.3); those sources are not
  // modelled, so such a register only captures through nothing.  On channel
  // 2 they are plain pin edges.
  if (code >= 12 && c != 2) return;
  const unsigned edge = code & 3;  // 0 rising, 1 falling, 2/3 both edges
  if (edge < 2 && (edge == 0) != level) return;
  sync(clock_.now());
  capture(c, pin, now_);
  update_requests();
  reschedule();
}

bool Mtu::output(unsigned c, unsigned pin) {
  sync(clock_.now());
  return c < channels_ && pin < 4 ? ch_[c].out[pin] : false;
}

void Mtu::dmac_clear(IrqSrc src) {
  const int c = src == IrqSrc::Tgi0a ? 0 : src == IrqSrc::Tgi1a ? 1 : src == IrqSrc::Tgi2a ? 2 : -1;
  if (c < 0) return;
  sync(clock_.now());
  ch_[c].tsr &= u8(~kTgfa);
  ch_[c].flags_read &= u8(~kTgfa);
  update_requests();
  reschedule();
}

u16 Mtu::tcnt(unsigned c) {
  sync(clock_.now());
  return ch_[c].tcnt;
}

u8 Mtu::tsr(unsigned c) {
  sync(clock_.now());
  const Channel& ch = ch_[c];
  return u8(ch.tsr | 0x40 | ((c == 0 || ch.up) ? 0x80 : 0));
}

// ---------------------------------------------------------------------------
// Registers

int Mtu::channel_of(u32 addr, u32& off) const {
  for (unsigned c = 0; c < 3; ++c)
    if (addr >= kBases[c] && addr < kBases[c] + kSpan[c]) { off = addr - kBases[c]; return int(c); }
  if (channels_ > 3 && addr >= kBase34 && addr < kBase34 + 0x30) {
    const BlockReg r = kBlock34[addr - kBase34];
    if (r.ch == 0xFF) return -2;  // stored-only register of the block
    off = r.off;
    return int(r.ch);
  }
  return -1;
}

u8 Mtu::read8(u32 addr) {
  sync(clock_.now());  // count and flags as of the accessing instruction
  if (addr == kTstr) return tstr_;
  if (addr == kTsyr) return tsyr_;
  u32 off;
  const int c = channel_of(addr, off);
  if (c == -2) return misc34_[addr - kBase34];
  return c < 0 ? 0xFF : read_reg(unsigned(c), off);
}

u8 Mtu::read_reg(unsigned c, u32 off) {
  Channel& ch = ch_[c];
  switch (off) {
    case kTcr: return ch.tcr;
    case kTmdr: return ch.tmdr;
    case kTiorH: return ch.tior[0];
    case kTiorL: return four_tgr(c) ? ch.tior[1] : 0xFF;
    case kTier: return ch.tier;
    case kTsr: {
      // Reading a flag as 1 arms its clear by a following 0 write.
      ch.flags_read |= ch.tsr;
      return u8(ch.tsr | 0x40 | ((c == 0 || ch.up) ? 0x80 : 0));  // TCFD reads 1 on channel 0
    }
    default:
      break;
  }
  // 16-bit registers: byte access is prohibited (10.3.1); reads return the byte anyway.
  if (off >= kTcnt && off < kTcnt + 2 + 2 * ntgr(c)) {
    const u16 v = off < kTgrA ? ch.tcnt : ch.tgr[(off - kTgrA) >> 1];
    return (off & 1) ? u8(v) : u8(v >> 8);
  }
  return 0xFF;
}

u16 Mtu::read16(u32 addr) {
  u32 off;
  const int c = channel_of(addr, off);
  if (c >= 0 && off >= kTcnt && !(off & 1) && off < kTcnt + 2 + 2 * ntgr(unsigned(c))) {
    sync(clock_.now());
    return off == kTcnt ? ch_[c].tcnt : ch_[c].tgr[(off - kTgrA) >> 1];
  }
  return Device::read16(addr);  // 8-bit register pairs (10.3.2)
}

void Mtu::write8(u32 addr, u8 v) {
  sync(clock_.now());
  if (addr == kTstr) {
    const u8 old = tstr_;
    tstr_ = v & (channels_ > 3 ? 0xC7 : 0x07);  // CST2-0 (+ CST4/CST3 on the 5-channel MTU); the rest read 0
    // A started counter takes its first count on the next tick of its grid
    // (figure 10.34); a stopped one keeps its value and output levels.
    for (unsigned c = 0; c < channels_; ++c)
      if (!((old >> cst_bit(c)) & 1) && internal_clock(c)) ch_[c].tick = tick_of(c, now_);
    reschedule();
    return;
  }
  if (addr == kTsyr) {
    tsyr_ = v & (channels_ > 3 ? 0xC7 : 0x07);
    reschedule();
    return;
  }
  u32 off;
  const int c = channel_of(addr, off);
  if (c == -2) misc34_[addr - kBase34] = v;
  else if (c >= 0) write_reg(unsigned(c), off, v);
}

void Mtu::write_reg(unsigned c, u32 off, u8 v) {
  Channel& ch = ch_[c];
  switch (off) {
    case kTcr:
      ch.tcr = four_tgr(c) ? v : u8(v & 0x7F);  // bit 7 reserved on channels 1, 2
      // Re-base the tick onto the (possibly new) grid; the changeover glitch of
      // a running prescaler switch is not modelled.
      if (internal_clock(c)) ch.tick = tick_of(c, now_);
      reschedule();
      break;
    case kTmdr:
      ch.tmdr = u8(0xC0 | (v & (four_tgr(c) ? 0x3F : 0x0F)));  // bits 7, 6 read 1; BFB/BFA on the 4-TGR channels
      if (internal_clock(c)) ch.tick = tick_of(c, now_);   // e.g. leaving phase counting mode
      reschedule();
      break;
    case kTiorH:
      write_tior(c, 0, v);
      break;
    case kTiorL:
      if (four_tgr(c)) write_tior(c, 1, v);
      break;
    case kTier:
      ch.tier = u8(0x40 | (v & (four_tgr(c) ? 0x9F : 0xB3)));  // bit 6 reads 1; TCIEU / TGIEC-D per channel
      update_requests();  // a newly enabled interrupt whose flag is set requests at once
      reschedule();
      break;
    case kTsr: {
      // A flag clears when written 0 after having been read as 1; 1s cannot be written.
      u8 cleared = 0;
      for (u8 bit = 0x01; bit && bit <= 0x20; bit = u8(bit << 1))
        if (!(v & bit) && (ch.flags_read & bit)) cleared |= bit;
      ch.tsr &= u8(~cleared);
      ch.flags_read &= u8(~cleared);
      update_requests();
      reschedule();
      break;
    }
    default:
      // TCNT / TGR: byte writes are prohibited (10.2.6, 10.2.7) and dropped here.
      break;
  }
}

void Mtu::write_tior(unsigned c, unsigned idx, u8 v) {
  Channel& ch = ch_[c];
  ch.tior[idx] = v;
  // A TIOR write while the counter is stopped drives the initial output level
  // (10.2.8 note).  Not initialised: buffer registers, the TIOC*B/D side in
  // PWM mode 1 and the period register's pin in PWM mode 2 (10.8.4).
  if (running(c)) { reschedule(); return; }
  for (unsigned x = idx * 2; x < idx * 2 + 2; ++x) {
    if (!is_compare(c, x) || !(io(c, x) & 3)) continue;
    if (mode(c) == kModePwm1 && (x & 1)) continue;
    if (mode(c) == kModePwm2 && clear_source(c) == int(x)) continue;
    set_output(c, x, (io(c, x) >> 2) & 1, now_);
  }
  reschedule();
}

void Mtu::write_tcnt(unsigned c, u16 v) {
  // Synchronous preset (10.4.3): a write to a synchronised channel's TCNT lands
  // in every synchronised channel.  10.7.3 / 10.7.4 / 10.7.12 (a clear or
  // count arriving in the write cycle) are simplified: the write always wins
  // over a count at the same state, the clear having been replayed already.
  if (synced(c)) {
    for (unsigned j = 0; j < channels_; ++j)
      if (synced(j)) ch_[j].tcnt = v;
  } else {
    ch_[c].tcnt = v;
  }
}

void Mtu::write16(u32 addr, u16 v) {
  u32 off;
  const int c = channel_of(addr, off);
  if (c >= 0 && off >= kTcnt && !(off & 1) && off < kTcnt + 2 + 2 * ntgr(unsigned(c))) {
    sync(clock_.now());
    if (off == kTcnt) write_tcnt(unsigned(c), v);
    else ch_[c].tgr[(off - kTgrA) >> 1] = v;  // 10.7.9: a match on the old value at this state already fired
    reschedule();
    return;
  }
  Device::write16(addr, v);
}

}  // namespace sh2

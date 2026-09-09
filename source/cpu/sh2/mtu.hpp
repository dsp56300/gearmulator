// SH7014 multifunction timer pulse unit, 3-channel MTU (hardware manual section
// 10), extended to the SH7042's five channels (SH7040 manual section 12):
// channels 3 and 4 have four TGRs each and share the interleaved register
// block at H'FFFF8200 (TCR3 +0, TCR4 +1, TMDR +2/3, TIORH/L +4-7, TIER +8/9,
// TCNT3 +10, TCNT4 +12, TGR3A/B +18/1A, TGR4A/B +1C/1E, TGR3C/D +24/26,
// TGR4C/D +28/2A, TSR3/4 +2C/2D) with CST3/CST4 in TSTR bits 6/7.  Their
// complementary / reset-synchronised PWM registers (TOER, TOCR, TGCR, TCDR,
// TDDR, TCNTS, TCBR) are stored only.
//
// Registers (table 10.3):
//   H'FFFF8240 TSTR  - - - - - CST2 CST1 CST0          H'FFFF8241 TSYR  - - - - - SYNC2 SYNC1 SYNC0
//   channel base +0 TCR   CCLR2 CCLR1 CCLR0 CKEG1 CKEG0 TPSC2 TPSC1 TPSC0   (bit 7 reserved on 1/2)
//                +1 TMDR  1 1 BFB BFA MD3 MD2 MD1 MD0                     (BFB/BFA channel 0 only)
//                +2 TIOR(H) IOB3-0 IOA3-0    +3 TIOR0L IOD3-0 IOC3-0      (channel 0 only)
//                +4 TIER  TTGE 1 TCIEU TCIEV TGIED TGIEC TGIEB TGIEA
//                +5 TSR   TCFD 1 TCFU TCFV TGFD TGFC TGFB TGFA           (flags: read 1 then write 0 clears)
//                +6 TCNT  +8 TGRA  +A TGRB  +C TGRC  +E TGRD              (16-bit; C/D channel 0 only)
//   channel bases: H'FFFF8260 (0), H'FFFF8280 (1), H'FFFF82A0 (2).
//
// Timing model (same idea as the H8 free-running timer).  A counter on an
// internal clock increments on every Nth state of the global clock (N = 1, 4,
// 16, 64, 256, 1024 by TPSC; CKEG shifts the grid by half a period or doubles
// the rate).  Between "special" ticks -- the tick that moves the count off a
// TGR value (compare match, possibly with a clear) or off H'FFFF (overflow) --
// the count is a linear function of time, so the module never ticks: it
// resolves its state lazily on register access and keeps ONE scheduler event,
// for the earliest special of any channel that can raise an enabled interrupt
// (or trigger the A/D converter / the output sink).  Because channels interact
// (synchronous clear, cascade), all three are always synchronised together,
// replaying their specials in time order.  Externally clocked and phase
// counting channels have no grid: they step on the host pin calls.
//
// Modelled: TPSC internal clocks and TCLKA-D external clocks with CKEG edges,
// CCLR (including synchronous clear through TSYR), synchronous preset, normal /
// PWM 1 / PWM 2 / phase counting 1-4 modes, buffer operation (BFA/BFB), output
// compare 0/1/toggle output, input capture on the TIOC pins, TGF/TCFV/TCFU/TCFD
// flags with TIER-gated level interrupts, TTGE A/D trigger, cascade connection
// (channel 1 clocked by TCNT2 overflow/underflow, 10.4.5).
// Approximations (see the comments referencing 10.x in mtu.cpp): the cross-
// channel input capture sources (TIOR codes 11xx on channels 0 and 1) never
// capture; the write/count contention rules of 10.7 are reduced to "the access
// happens after the state of its cycle is resolved"; the pin-width limits of
// 10.7.1 are not checked; the complementary / reset-synchronised PWM modes of
// the 5-channel MTU do not exist on the SH7014 and are not present.
#pragma once
#include <array>
#include <functional>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/intc.hpp"

namespace sh2 {

class Mtu final : public Device {
 public:
  static constexpr u32 kTstr = 0xFFFF8240u, kTsyr = 0xFFFF8241u;
  static constexpr u32 kBase0 = 0xFFFF8260u, kBase1 = 0xFFFF8280u, kBase2 = 0xFFFF82A0u, kBase34 = 0xFFFF8200u;
  static constexpr unsigned kMaxChannels = 5;
  // Register offsets from a channel base.
  enum Reg : u32 { kTcr = 0, kTmdr = 1, kTiorH = 2, kTiorL = 3, kTier = 4, kTsr = 5, kTcnt = 6,
                   kTgrA = 8, kTgrB = 10, kTgrC = 12, kTgrD = 14 };
  // TCR bits
  static constexpr u8 kCclrMask = 0xE0, kCkegMask = 0x18, kTpscMask = 0x07;
  // TMDR bits
  static constexpr u8 kBfb = 0x20, kBfa = 0x10, kMdMask = 0x0F;
  enum Mode : u8 { kModeNormal = 0, kModePwm1 = 2, kModePwm2 = 3, kModePhase1 = 4, kModePhase2 = 5,
                   kModePhase3 = 6, kModePhase4 = 7 };
  // TIER bits
  static constexpr u8 kTtge = 0x80, kTcieu = 0x20, kTciev = 0x10, kTgied = 0x08, kTgiec = 0x04,
                      kTgieb = 0x02, kTgiea = 0x01;
  // TSR bits
  static constexpr u8 kTcfd = 0x80, kTcfu = 0x20, kTcfv = 0x10, kTgfd = 0x08, kTgfc = 0x04,
                      kTgfb = 0x02, kTgfa = 0x01;

  // Output pin change: (channel, pin 0-3 = TIOCxA-D, level, time of the change).
  using OutputSink = std::function<void(unsigned ch, unsigned pin, bool level, u64 at)>;
  // A/D conversion start request (TTGE and TGRA compare match / input capture): (channel, time).
  using TriggerSink = std::function<void(unsigned ch, u64 at)>;

  // channels: 3 (SH7014) or 5 (SH7042).
  Mtu(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, unsigned channels = 3);
  ~Mtu() override;

  void map(emu::IoMux& mux);
  void reset();

  // --- bus -------------------------------------------------------------------
  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;
  void write16(u32 addr, u16 value) override;

  // --- pins --------------------------------------------------------------------
  // TCLKA-D (pin 0-3) level; edges clock the channels that select the pin (TPSC,
  // CKEG) and drive the phase counting modes (table 10.8: channel 1 on A/B,
  // channel 2 on C/D).
  void set_tclk(unsigned pin, bool level);
  // Convenience: one edge on TCLK pin `pin` (true = rising).
  void external_clock_edge(unsigned pin, bool rising) { set_tclk(pin, rising); }
  // TIOCxA-D (pin 0-3) input level for input capture.
  void capture_edge(unsigned ch, unsigned pin, bool level);
  // Current TIOCxA-D output compare level.
  bool output(unsigned ch, unsigned pin);
  void set_output_sink(OutputSink s) { sync(clock_.now()); out_sink_ = std::move(s); reschedule(); }
  void set_adc_trigger(TriggerSink s) { sync(clock_.now()); adc_sink_ = std::move(s); reschedule(); }

  // DMAC cooperation: a transfer activated by TGIxA clears TGFA (table 10.13).
  void dmac_clear(IrqSrc src);

  // --- introspection (values as of now) ---------------------------------------
  u16 tcnt(unsigned ch);
  u16 tgr(unsigned ch, unsigned i) const { return ch_[ch].tgr[i]; }
  u8 tsr(unsigned ch);
  u8 tstr() const { return tstr_; }
  u8 tsyr() const { return tsyr_; }

 private:
  struct Channel {
    u8 tcr = 0, tmdr = 0xC0, tier = 0x40, tsr = 0;  // tsr holds only the flag bits
    std::array<u8, 2> tior{};                       // [0] = TIORH / TIOR, [1] = TIOR0L
    u8 flags_read = 0;                              // flags read as 1 since their last clear
    u16 tcnt = 0;
    std::array<u16, 4> tgr{0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    std::array<bool, 4> out{};                      // TIOC output levels
    std::array<bool, 4> in{};                       // TIOC input levels (capture)
    bool up = true;                                 // TCFD
    u64 tick = 0;                                   // grid tick at which tcnt is valid
  };
  struct Grid { unsigned shift; u64 offset; };      // tick k at time (k << shift) - offset

  static void on_event(void* self, u64 when, u64 now);

  // --- per-channel decoding -----------------------------------------------------
  unsigned ntgr(unsigned c) const { return (c == 0 || c >= 3) ? 4 : 2; }
  static bool four_tgr(unsigned c) { return c == 0 || c >= 3; }
  // TSTR / TSYR bit of channel c (CST3/CST4 are bits 6/7).
  static unsigned cst_bit(unsigned c) { return c < 3 ? c : c + 3; }
  u8 mode(unsigned c) const { return ch_[c].tmdr & kMdMask; }
  bool phase_mode(unsigned c) const { return (c == 1 || c == 2) && (mode(c) & 0x0C) == 0x04; }
  bool running(unsigned c) const { return (tstr_ >> cst_bit(c)) & 1; }
  bool synced(unsigned c) const { return (tsyr_ >> cst_bit(c)) & 1; }
  bool cascaded(unsigned c) const { return c == 1 && (ch_[1].tcr & kTpscMask) == 7 && !phase_mode(1); }
  bool internal_clock(unsigned c) const;   // counts on the phi grid
  int external_pin(unsigned c) const;      // TCLK pin index selected by TPSC, or -1
  Grid grid(unsigned c) const;
  u64 tick_of(unsigned c, u64 t) const { const Grid g = grid(c); return (t + g.offset) >> g.shift; }
  u64 time_of(unsigned c, u64 k) const { const Grid g = grid(c); return (k << g.shift) - g.offset; }
  u8 io(unsigned c, unsigned x) const { return u8((ch_[c].tior[x >> 1] >> ((x & 1) * 4)) & 0xF); }
  bool is_buffer(unsigned c, unsigned x) const {
    return four_tgr(c) && ((x == 2 && (ch_[c].tmdr & kBfa)) || (x == 3 && (ch_[c].tmdr & kBfb)));
  }
  bool is_compare(unsigned c, unsigned x) const { return !(io(c, x) & 8) && !is_buffer(c, x); }
  bool is_capture(unsigned c, unsigned x) const { return (io(c, x) & 8) && !is_buffer(c, x); }
  bool sync_clear_selected(unsigned c) const { return ((ch_[c].tcr >> 5) & 3) == 3; }
  int clear_source(unsigned c) const;      // TGR index whose match/capture clears TCNT, or -1
  bool matches(unsigned c, unsigned x) const { return is_compare(c, x) && ch_[c].tcnt == ch_[c].tgr[x]; }
  bool own_clear(unsigned c) const { const int s = clear_source(c); return s >= 0 && matches(c, unsigned(s)); }

  // --- counter model --------------------------------------------------------------
  void sync(u64 now);
  u64 ticks_to_next_special(unsigned c) const;
  u64 next_special_time(unsigned c) const;
  // Apply the count clock that moves TCNT off its current value; true if the
  // channel's own clear source fired.
  bool transition(unsigned c, bool up, bool sync_clear, u64 at);
  void clear_counter(unsigned c, u64 at);
  void propagate_sync_clear(unsigned src, u64 at);
  void set_flag(unsigned c, u8 flag, u64 at);
  void set_output(unsigned c, unsigned pin, bool level, u64 at);
  void capture(unsigned c, unsigned x, u64 at);
  void step_external(unsigned c, bool up, u64 at);  // external clock / phase / cascade count
  void update_requests();
  void reschedule();
  u64 next_interesting_time(unsigned c) const;

  // --- registers ---------------------------------------------------------------------
  int channel_of(u32 addr, u32& off) const;
  u8 read_reg(unsigned c, u32 off);
  void write_reg(unsigned c, u32 off, u8 v);
  void write_tcnt(unsigned c, u16 v);
  void write_tior(unsigned c, unsigned idx, u8 v);

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  emu::Scheduler::EventId event_ = 0;
  OutputSink out_sink_;
  TriggerSink adc_sink_;

  unsigned channels_;
  std::array<Channel, kMaxChannels> ch_{};
  std::array<u8, 0x30> misc34_{};  // channel 3/4 block registers that are only stored
  u8 tstr_ = 0, tsyr_ = 0;
  std::array<bool, 4> tclk_{};  // TCLKA-D levels
  u64 now_ = 0;                 // time up to which the module is synchronised
};

}  // namespace sh2

// SH7034 16-bit integrated timer pulse unit, 5-channel ITU (SH7032/34
// hardware manual section 10).
//
// Registers (H'5FFFF00-H'5FFFF3F):
//   +00 TSTR  - - - STR4 STR3 STR2 STR1 STR0     +01 TSNC  +02 TMDR  +03 TFCR  +31 TOCR
//   channel base +0 TCR   - CCLR1 CCLR0 CKEG1 CKEG0 TPSC2 TPSC1 TPSC0
//                +1 TIOR  - IOB2 IOB1 IOB0 - IOA2 IOA1 IOA0
//                +2 TIER  1 1 1 1 1 OVIE IMIEB IMIEA
//                +3 TSR   1 1 1 1 1 OVF IMFB IMFA          (flags: read 1 then write 0 clears)
//                +4 TCNT  +6 GRA  +8 GRB  (+A BRA  +C BRB on channels 3 and 4)
//   channel bases: +04 (0), +0E (1), +18 (2), +22 (3), +32 (4).
//
// Timing model (the MTU's): a counter on an internal clock increments on every
// Nth state (N = 1, 2, 4, 8 by TPSC2-0 = 0-3); between "special" ticks -- the
// count reaching GRA or GRB (IMFA / IMFB, and the clear when CCLR selects
// that register: the count holds the match value for one clock, then goes to
// 0) or wrapping from H'FFFF (OVF) -- the count is a linear function of time,
// so the module resolves its state lazily on register access and keeps one
// scheduler event for the earliest special that can raise an enabled
// interrupt.  External clocks (TPSC2 = 1) count on tclk() calls per CKEG.
// Stored but without effect: TIOR (no compare-match output or input capture
// pins are modelled), TSNC / TMDR / TFCR / TOCR (synchronous operation, PWM,
// buffer and complementary modes), BRA / BRB.
#pragma once
#include <array>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/intc.hpp"

namespace sh2 {

class Itu final : public Device {
 public:
  static constexpr u32 kBase = 0x05FFFF00u;
  static constexpr unsigned kChannels = 5;
  static constexpr u32 kChannelOffset[kChannels] = {0x04, 0x0E, 0x18, 0x22, 0x32};
  enum Reg : u32 { kTcr = 0, kTior = 1, kTier = 2, kTsr = 3, kTcnt = 4, kGra = 6, kGrb = 8, kBra = 10, kBrb = 12 };
  // TCR bits
  static constexpr u8 kCclrMask = 0x60, kCkegMask = 0x18, kTpscMask = 0x07;
  // TIER / TSR bits
  static constexpr u8 kImfa = 0x01, kImfb = 0x02, kOvf = 0x04, kFlagMask = 0x07;

  Itu(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc);
  ~Itu() override;

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;
  void write16(u32 addr, u16 value) override;

  // --- host side -----------------------------------------------------------
  // External clock pin TCLKA-D (n = 0-3) level.
  void tclk(unsigned n, bool high);
  // The DMAC served an IMIAn request: the flag is cleared (9.3.2).
  void dmac_clear(IrqSrc src);

  u16 tcnt(unsigned ch) { sync(clock_.now()); return ch_[ch].tcnt; }
  u8 tsr(unsigned ch) { sync(clock_.now()); return ch_[ch].tsr; }
  u8 tstr() const { return tstr_; }

 private:
  struct Channel {
    u8 tcr = 0, tior = 0x08, tier = 0xF8, tsr = 0xF8, tsr_read = 0;
    u16 tcnt = 0, gra = 0xFFFF, grb = 0xFFFF, bra = 0xFFFF, brb = 0xFFFF;
    u64 tick = 0;  // clock tick at which tcnt was last resolved
  };

  static void on_event(void* self, u64 when, u64 now);
  bool running(unsigned ch) const { return (tstr_ >> ch) & 1; }
  bool internal(unsigned ch) const { return !(ch_[ch].tcr & 0x04); }
  unsigned shift(unsigned ch) const { return ch_[ch].tcr & 0x03; }
  u64 tick_of(unsigned ch, u64 now) const { return now >> shift(ch); }
  void sync(u64 now);
  void advance(unsigned ch, u64 ticks);
  // Ticks until the next special of the channel; `enabled_only` restricts to
  // specials that raise an enabled interrupt.  ~0 when there is none.
  u64 distance(unsigned ch, bool enabled_only) const;
  void step_once(unsigned ch);  // one count (external clock)
  void reschedule();
  void update_request(unsigned ch);
  void start(unsigned ch, u64 now);

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  emu::Scheduler::EventId event_ = 0;
  u64 now_ = 0;
  u8 tstr_ = 0, tsnc_ = 0, tmdr_ = 0, tfcr_ = 0, tocr_ = 0xFF;
  std::array<Channel, kChannels> ch_{};
  std::array<bool, 4> tclk_high_{};
};

}  // namespace sh2

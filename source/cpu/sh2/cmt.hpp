// Compare match timer (SH7014 hardware manual section 15).
//
// Two 16-bit up-counters clocked by phi/8, phi/32, phi/128 or phi/512 (CKS1-0
// of the channel's CMCSR) while the channel's STR bit in CMSTR is set.  When
// CMCNT holds the CMCOR value and the next input clock arrives, the counter
// returns to H'0000 instead of incrementing and CMF is set (15.4.2: the match
// signal is generated on the count that would follow the matching value, so
// the period is CMCOR + 1 input clocks).  CMI0 / CMI1 are requested while
// CMF && CMIE; CMF clears by writing 0 after reading 1.
//
// Registers (16-bit, also accessible by byte and by longword pairs):
//   H'FFFF83D0 CMSTR   -------------- STR1 STR0                reset H'0000
//   H'FFFF83D2 CMCSR0  -------- CMF CMIE ---- CKS1 CKS0        reset H'0000
//   H'FFFF83D4 CMCNT0                                          reset H'0000
//   H'FFFF83D6 CMCOR0                                          reset H'FFFF
//   H'FFFF83D8 CMCSR1, H'FFFF83DA CMCNT1, H'FFFF83DC CMCOR1
//
// Same lazy model as the H8 timers: the count is a linear function of the
// global state clock between matches (the prescaler is free-running, so a
// count happens at every multiple of the divider), synced on register access,
// and one scheduler event per channel marks the next interrupt-capable match.
#pragma once
#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/intc.hpp"

namespace sh2 {

class Cmt final : public Device {
 public:
  static constexpr u32 kBase = 0xFFFF83D0u;
  static constexpr u32 kCmstr = kBase, kCmcsr0 = kBase + 2, kCmcnt0 = kBase + 4, kCmcor0 = kBase + 6;
  static constexpr u32 kCmcsr1 = kBase + 8, kCmcnt1 = kBase + 10, kCmcor1 = kBase + 12;
  // CMSTR bits
  static constexpr u16 kStr0 = 0x0001, kStr1 = 0x0002;
  // CMCSR bits
  static constexpr u16 kCmf = 0x0080, kCmie = 0x0040, kCksMask = 0x0003;

  Cmt(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc);
  ~Cmt() override;

  void map(emu::IoMux& mux);
  void reset();  // power-on reset values (standby-mode initialisation is the integrator's call)

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;
  void write16(u32 addr, u16 value) override;

  // --- host side --------------------------------------------------------------
  u16 cmstr() const { return cmstr_; }
  u16 cmcsr(unsigned ch, u64 now) { sync(ch_[ch & 1], now); return ch_[ch & 1].cmcsr; }
  u16 cmcnt(unsigned ch, u64 now) { sync(ch_[ch & 1], now); return ch_[ch & 1].cmcnt; }
  u16 cmcor(unsigned ch) const { return ch_[ch & 1].cmcor; }

 private:
  struct Channel {
    unsigned index = 0;
    IrqSrc src = IrqSrc::Cmi0;
    emu::Scheduler::EventId event = 0;
    u16 cmcsr = 0;
    u16 cmcnt = 0;
    u16 cmcor = 0xFFFF;
    bool cmf_read = false;  // CMF has been read as 1 since it was last written
    u64 tick = 0;           // prescaler tick at which `cmcnt` is valid
    u64 now = 0;            // state at which the channel was last synced
  };

  static void on_event0(void* self, u64 when, u64 now);
  static void on_event1(void* self, u64 when, u64 now);
  static unsigned period_shift(const Channel& c) { return 3 + 2 * (c.cmcsr & kCksMask); }  // phi/8, 32, 128, 512
  bool running(const Channel& c) const { return (cmstr_ >> c.index) & 1; }
  static u64 tick_of(const Channel& c, u64 t) { return t >> period_shift(c); }
  void sync(Channel& c, u64 now);
  void update_request(Channel& c);
  void reschedule(Channel& c);
  void write_cmcsr(Channel& c, u16 value, u16 mask);
  void write_cmcnt(Channel& c, u16 value, u16 mask);
  void write_cmstr(u16 value);

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  u16 cmstr_ = 0;
  Channel ch_[2];
};

}  // namespace sh2

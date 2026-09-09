// 8-bit timer (H8/510 hardware manual section 11).
//
// Registers at H'FEC0:
//   +0 TCR    CMIEB CMIEA OVIE CCLR1 CCLR0 CKS2 CKS1 CKS0
//   +1 TCSR   CMFB  CMFA  OVF  1     OS3   OS2  OS1  OS0   (flags: read 1 then write 0 clears)
//   +2 TCORA  +3 TCORB  +4 TCNT
// emu::Clock: stopped, phi/8, phi/64, phi/1024, or the TMCI pin on the rising,
// falling or both edges.  TCNT can be cleared on compare match A or B or on
// the rising edge of the TMRI pin.  The TMO output changes on each compare
// match as selected by OS3-OS0 (toggle > 1 > 0 > no change when A and B
// match together).
//
// Same lazy model as the free-running timer: the count is linear in the global
// state clock between the ticks that leave TCORA, TCORB or H'FF, and one
// scheduler event covers the next interrupt-capable tick.
#pragma once
#include "cpu/h8500/bus.hpp"
#include "common/iomux.hpp"
#include "cpu/h8500/intc.hpp"
#include "common/sched.hpp"

namespace h8500 {

class Tmr final : public Device {
 public:
  // TCR bits
  static constexpr u8 kCmieb = 0x80, kCmiea = 0x40, kOvie = 0x20;
  // TCSR bits
  static constexpr u8 kCmfb = 0x80, kCmfa = 0x40, kOvf = 0x20;

  struct Sources { IrqSrc cmia, cmib, ovi; };

  Tmr(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base, Sources src);
  ~Tmr() override;

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  // --- pins --------------------------------------------------------------
  void external_clock_edge(bool rising);   // TMCI
  void external_reset_edge();              // TMRI rising edge (counts when CCLR = 11)
  bool output() { sync(clock_.now()); return out_; }  // TMO

  void dtc_clear(IrqSrc src);

  u8 tcnt(u64 now) { sync(now); return tcnt_; }
  u8 tcsr(u64 now) { sync(now); return u8(tcsr_ | 0x10); }

 private:
  static void on_event(void* self, u64 when, u64 now);
  unsigned period_shift() const;  // 0 = no internal clock
  bool internal_clock() const { return period_shift() != 0; }
  u64 tick_of(u64 t) const { return t >> period_shift(); }
  void sync(u64 now);
  void transition();
  u64 ticks_to_next_special() const;
  void update_requests();
  void reschedule();

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  u32 base_;
  Sources src_;
  emu::Scheduler::EventId event_ = 0;

  u8 tcr_ = 0;
  u8 tcsr_ = 0;        // bit 4 stored as 0, read as 1
  u8 flags_read_ = 0;
  u8 tcora_ = 0xFF;
  u8 tcorb_ = 0xFF;
  u8 tcnt_ = 0;
  u64 tick_ = 0;
  u64 now_ = 0;
  bool out_ = false;
};

}  // namespace h8500

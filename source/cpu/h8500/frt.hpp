// 16-bit free-running timer (H8/510 hardware manual section 10).
//
// Registers (offsets from the channel base, H'FEA0 for FRT1 / H'FEB0 for FRT2):
//   +0 TCR   ICIE OCIEB OCIEA OVIE OEB OEA CKS1 CKS0
//   +1 TCSR  ICF  OCFB  OCFA  OVF  OLVLB OLVLA IEDG CCLRA   (flags: read 1 then write 0 clears)
//   +2 FRC   16-bit counter, +4 OCRA, +6 OCRB, +8 ICR (read-only)
// The 16-bit registers sit on an 8-bit module bus and go through an 8-bit
// TEMP latch: write high byte (latched) then low byte (both stored); read high
// byte (low latched) then low byte (from TEMP).  OCRA/OCRB reads bypass TEMP.
//
// Timing model.  The counter increments on every Nth state of the global
// state counter (N = 4, 8 or 32 by CKS; the "prescaler grid" is why the
// manual's FRC synchronisation recipes work), or on external pulses.  Between
// "special" ticks -- the tick that moves the counter off OCRA (compare match
// A, optionally clearing it), off OCRB (match B) or off H'FFFF (overflow) --
// the count is a linear function of time, so the module never ticks: it
// resolves its value lazily on register access and schedules exactly one
// scheduler event, for the next special tick that can raise an enabled
// interrupt.  Flags that nobody waits for are set retroactively on the next
// access, which is indistinguishable to the program.
#pragma once
#include "cpu/h8500/bus.hpp"
#include "common/iomux.hpp"
#include "cpu/h8500/intc.hpp"
#include "common/sched.hpp"

namespace h8500 {

class Frt final : public Device {
 public:
  // TCR bits
  static constexpr u8 kIcie = 0x80, kOcieb = 0x40, kOciea = 0x20, kOvie = 0x10, kOeb = 0x08, kOea = 0x04;
  // TCSR bits
  static constexpr u8 kIcf = 0x80, kOcfb = 0x40, kOcfa = 0x20, kOvf = 0x10, kOlvlb = 0x08, kOlvla = 0x04,
                      kIedg = 0x02, kCclra = 0x01;

  struct Sources { IrqSrc ici, ocia, ocib, fovi; };

  Frt(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base, Sources src);
  ~Frt() override;

  void map(emu::IoMux& mux);
  void reset();

  // --- bus ---------------------------------------------------------------
  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  // --- pins --------------------------------------------------------------
  // Edge on the FTI input (true = rising).  Captures when it matches IEDG.
  void capture_edge(bool rising);
  // One rising edge on the FTCI external clock input (used when CKS = 3).
  void external_clock_pulse();
  // Current output-compare pin levels (FTOA / FTOB).
  bool output_a() { sync(clock_.now()); return out_a_; }
  bool output_b() { sync(clock_.now()); return out_b_; }

  // --- DTC cooperation: the DTC clears the flag of the interrupt it served.
  void dtc_clear(IrqSrc src);

  // --- introspection (values as of `now`) --------------------------------
  u16 frc(u64 now) { sync(now); return frc_; }
  u16 ocra() const { return ocra_; }
  u16 ocrb() const { return ocrb_; }
  u16 icr() const { return icr_; }
  u8 tcr() const { return tcr_; }
  u8 tcsr(u64 now) { sync(now); return tcsr_; }

 private:
  static void on_event(void* self, u64 when, u64 now);

  unsigned period_shift() const;                   // log2 of states per count, or 0 for external
  bool internal_clock() const { return (tcr_ & 3) != 3; }
  u64 tick_of(u64 t) const { return t >> period_shift(); }
  // Bring the counter and flags up to date with time `now`.
  void sync(u64 now);
  // Apply the transition that moves the counter off value `frc_` (match/clear/overflow).
  void transition();
  // Ticks until the next special transition from the current count, 0 if none.
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
  u8 tcsr_ = 0;
  u8 flags_read_ = 0;   // flag bits read as 1 since the last clear (arms write-0 clear)
  u16 frc_ = 0;
  u16 ocra_ = 0xFFFF;
  u16 ocrb_ = 0xFFFF;
  u16 icr_ = 0;
  u8 temp_ = 0;
  u64 tick_ = 0;        // prescaler tick index at which frc_ is valid
  u64 now_ = 0;         // last time the module was synchronised
  bool out_a_ = false;
  bool out_b_ = false;
};

}  // namespace h8500

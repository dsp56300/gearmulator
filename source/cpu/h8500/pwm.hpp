// PWM timer of the H8/570 (hardware manual section 10).
//
// A 16-bit up-counter TMR clocked by phi, phi/4, phi/8 or phi/16 (TCR.CKS),
// compared against OCR0-OCR2.  On the count clock after TMR == OCRn the
// output data register ODRn is transferred to the output latch ODL (one bit
// or all six, TCR.OMS), OCFn is set in TMSR, and with FRM set a match on
// OCR0 clears TMR (period OCR0 + 1).  OCFn with OCIEn requests OCIn; TRE0 /
// TRE2 route a match on OCR0 / OCR2 to the ISP's ICFH6 / ICFH7 flags.
//
// Registers (H'FEA0-H'FEAD): TCR, TMSR, ODL, ODR0-2, OCR0H/L, OCR1H/L,
// OCR2H/L, TMRH/L.  The 16-bit registers are written through a TEMP byte
// (high byte first); TMR is read the same way.
//
// The counter is a lazy function of the state clock; one scheduler event
// marks the next match.
#pragma once
#include <functional>

#include "cpu/h8500/bus.hpp"
#include "cpu/h8500/intc.hpp"
#include "common/iomux.hpp"
#include "common/sched.hpp"

namespace h8500 {

class Pwm final : public Device {
 public:
  // TCR bits
  static constexpr u8 kTce = 0x80, kFrm = 0x40, kOms = 0x08;
  // TMSR bits
  static constexpr u8 kTre2 = 0x80, kTre0 = 0x40, kOcie2 = 0x20, kOcie1 = 0x10, kOcie0 = 0x08;
  static constexpr u8 kOcf2 = 0x04, kOcf1 = 0x02, kOcf0 = 0x01;

  Pwm(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base = 0xFEA0);
  ~Pwm() override;

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  void write16(u32 addr, u16 value) override;

  // A match on OCR0 (n = 0) or OCR2 (n = 2) with its transfer-request enable
  // set: the ISP's interconnection flag would be set.
  void set_route_hook(std::function<void(unsigned n)> hook) { route_hook_ = std::move(hook); }

  u8 tcr() const { return tcr_; }
  u8 tmsr() const { return tmsr_; }
  u8 odl() const { return odl_; }
  u16 ocr(unsigned i) const { return ocr_[i]; }
  u16 tmr(u64 now) { sync(now); return tmr_; }

 private:
  static void on_event(void* self, u64 when, u64 now);
  void sync(u64 now);
  void match(unsigned i);
  void reschedule();
  void update_requests();
  unsigned shift() const { static constexpr u8 k[4] = {0, 2, 3, 4}; return k[(tcr_ >> 4) & 3]; }
  bool running() const { return (tcr_ & kTce) != 0; }
  // Count ticks until the next match, 1..65536.
  u32 ticks_to_next_match() const;

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  u32 base_;
  std::function<void(unsigned)> route_hook_;

  u8 tcr_ = 0, tmsr_ = 0, odl_ = 0;
  u8 odr_[3] = {0, 0, 0};
  u16 ocr_[3] = {0xFFFF, 0xFFFF, 0xFFFF};
  u16 tmr_ = 0;
  u8 temp_ = 0;
  u8 ocf_read_ = 0;    // OCF bits read as 1 (armed for a write-0 clear)
  u64 tick_ = 0;       // state of the count clock edge that produced tmr_
  emu::Scheduler::EventId event_ = 0;
};

}  // namespace h8500

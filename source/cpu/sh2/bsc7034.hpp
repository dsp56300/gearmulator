// SH7034 bus state controller (SH7032/34 hardware manual section 8).
//
// Registers (16-bit, H'5FFFFA0-H'5FFFFB3): BCR, WCR1, WCR2, WCR3, DCR, PCR,
// RCR, RTCSR, RTCNT, RTCOR.  The refresh-controller registers are written by
// a word whose upper byte is a key (RCR H'5A, RTCSR H'A5, RTCNT H'69, RTCOR
// H'96); byte writes to them are ignored.
//
// Timing (tables 8.4 / 8.5, WAIT pin high): a 16-bit bus everywhere.  Areas 1,
// 3, 4, 5 and 7: 1 state when the area's WCR1 RWn bit is 0, 2 states when it
// is 1.  Areas 0 and 2: 1 state + the A02LW long wait (1-4 states, WCR3);
// area 6 (multiplexed I/O): 4 states.  DRAM (area 1) is taken as an ordinary
// area with the short / long pitch selected by RW1.  Write cycles use the
// same figures; the WAIT pin is never asserted.  The on-chip ROM / RAM and the
// register field keep their fixed classes (1 and 3 states).  Every change
// retimes the eight area classes, which drops the decoded code.
// The refresh timer RTCNT counts at phi / 2, 8, 32, 128, 512, 2048, 4096
// (RTCSR CKS 1-7), sets CMF and restarts from 0 when it reaches RTCOR, and
// requests CMI while CMF and CMIE are set.  Parity checking (PCR, PEI) is
// stored only.
#pragma once
#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/intc.hpp"

namespace sh2 {

class Bsc7034 final : public Device {
 public:
  static constexpr u32 kBase = 0x05FFFFA0u;
  static constexpr u32 kBcr = kBase, kWcr1 = kBase + 2, kWcr2 = kBase + 4, kWcr3 = kBase + 6, kDcr = kBase + 8,
                       kPcr = kBase + 10, kRcr = kBase + 12, kRtcsr = kBase + 14, kRtcnt = kBase + 16, kRtcor = kBase + 18;
  // Access class of each area (see Bus::Class).
  static constexpr u8 kAreaClass[8] = {Bus::kClsCs0, Bus::kClsDram, Bus::kClsCs1, Bus::kClsCs2,
                                       Bus::kClsCs3, Bus::kClsArea5, Bus::kClsArea6, Bus::kClsArea7};
  // RTCSR bits
  static constexpr u8 kCmf = 0x80, kCmie = 0x40, kCksMask = 0x38;

  Bsc7034(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, Bus& bus);
  ~Bsc7034() override;

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;
  void write16(u32 addr, u16 value) override;

  u16 wcr1() const { return wcr1_; }
  u16 wcr3() const { return wcr3_; }
  u8 rtcnt() { sync(clock_.now()); return rtcnt_; }
  u8 rtcsr() const { return rtcsr_; }

 private:
  static void on_event(void* self, u64 when, u64 now);
  void install_classes();
  unsigned shift() const {
    static constexpr u8 kShift[8] = {0, 1, 3, 5, 7, 9, 11, 12};
    return kShift[(rtcsr_ & kCksMask) >> 3];
  }
  bool running() const { return (rtcsr_ & kCksMask) != 0; }
  void sync(u64 now);
  void reschedule();
  void update_request() { intc_.set_request(IrqSrc::Cmi, (rtcsr_ & kCmf) && (rtcsr_ & kCmie)); }

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  Bus& bus_;
  emu::Scheduler::EventId event_ = 0;
  u16 bcr_ = 0, wcr1_ = 0xFFFF, wcr2_ = 0xFFFF, wcr3_ = 0xF800, dcr_ = 0, pcr_ = 0;
  u8 rcr_ = 0, rtcsr_ = 0, rtcnt_ = 0, rtcor_ = 0xFF;
  bool cmf_read_ = false;
  u64 tick_ = 0, now_ = 0;
};

}  // namespace sh2

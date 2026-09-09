// A/D converter (H8/510 hardware manual section 14).
//
// Registers at H'FE90: ADDRA-ADDRD (16-bit, 10-bit result left-justified: AD9-2
// in the high byte, AD1-0 in bits 7-6 of the low byte; the low byte is read
// through TEMP after the high byte), ADCSR at +8 (ADF ADIE ADST SCAN CKS CH2
// CH1 CH0), ADCR at +9 (TRGE, other bits read 1).
//
// Single mode converts the selected channel once (266 states with CKS = 0,
// 134 with CKS = 1), sets ADF and clears ADST.  Scan mode converts AN0..ANn
// cyclically (first conversion 266/134 states, then 256/128 each) and sets
// ADF after each pass.  ADI = ADF & ADIE.  The analog values come from the
// host through set_input() or a sampler callback evaluated when a conversion
// completes.
#pragma once
#include <functional>

#include "cpu/h8500/bus.hpp"
#include "common/iomux.hpp"
#include "cpu/h8500/intc.hpp"
#include "common/sched.hpp"

namespace h8500 {

class Adc final : public Device {
 public:
  static constexpr u8 kAdf = 0x80, kAdie = 0x40, kAdst = 0x20, kScan = 0x10, kCks = 0x08;
  static constexpr u8 kTrge = 0x80;
  using Sampler = std::function<u16(unsigned channel)>;  // returns a 10-bit value

  Adc(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base);
  ~Adc() override;

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  // --- host side -----------------------------------------------------------
  void set_input(unsigned channel, u16 value10) { inputs_[channel & 3] = u16(value10 & 0x3FF); }
  // Width of the ADCSR channel-select field. The H8/510 selects one of four
  // inputs; the H8/532 has eight, in two scan groups sharing the four result
  // registers (its hardware manual, section 14).
  void set_channel_select_bits(unsigned bits) { ch_mask_ = u8((1u << bits) - 1); }
  void set_sampler(Sampler s) { sampler_ = std::move(s); }
  // High-to-low transition on ADTRG: starts a conversion when TRGE is set.
  void trigger();

  void dtc_clear();

  u16 result(unsigned channel) const { return addr_[channel & 3]; }
  u8 adcsr(u64 now) { sync(now); return adcsr_; }
  bool converting() { sync(clock_.now()); return (adcsr_ & kAdst) != 0; }

 private:
  static void on_event(void* self, u64 when, u64 now);
  void sync(u64 now);
  void start(u64 at);
  void stop();
  void reschedule();
  u64 conversion_states(bool first) const;
  u16 sample(unsigned ch) const;
  void update_request();
  // Result register for a channel: the H8/532's upper scan group shares the
  // four registers with the lower one.
  unsigned result_index() const { return channel_ & 3; }
  // First channel of the current conversion: a scan starts at the bottom of
  // the selected group, a single conversion is the selected channel itself.
  unsigned first_channel() const { return (adcsr_ & kScan) ? u8(adcsr_ & ch_mask_ & ~3u) : u8(adcsr_ & ch_mask_); }
  unsigned last_channel() const { return adcsr_ & ch_mask_; }

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  u32 base_;
  emu::Scheduler::EventId event_ = 0;
  Sampler sampler_;

  u8 ch_mask_ = 3;
  u16 addr_[4] = {};
  u16 inputs_[4] = {};
  u8 adcsr_ = 0;
  u8 adcr_ = 0;   // only TRGE stored
  u8 temp_ = 0;
  unsigned channel_ = 0;  // channel being converted
  u64 end_ = 0;           // completion time of the conversion in progress (0 = idle)
  u64 now_ = 0;
};

}  // namespace h8500

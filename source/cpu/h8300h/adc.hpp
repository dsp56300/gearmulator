// A/D converter of the H8/3002 (ADDRA-ADDRD, ADCSR and ADCR at H'FFFFE0-E9).  The chip leaves it to the board
// (machine.hpp): the board maps it with Machine::map_register() and supplies the analog inputs.
//
// A conversion takes 134 or 266 states (ADCSR.CKS) and sets ADF, which with ADIE is the ADI request, a level;
// scan mode repeats it.  A data register reads its input when it is read: register n holds AN n of the group
// ADCSR.CH2 selects, the board's `source(channel)` (channel 0-7 = AN0-AN7) returns its 10-bit value.
#pragma once
#include <functional>

#include "common/device.hpp"
#include "cpu/h8300h/machine.hpp"

namespace h8300h {

class Adc final : public emu::Device {
 public:
  using Source = std::function<int(unsigned channel)>;

  static constexpr u32 kBase = 0xFFFFE0, kSize = 10;

  Adc(Machine& machine, Source source);
  ~Adc() override { cancel_conversion(); }

  void reset();
  u8 read8(u32 a) override;
  void write8(u32 a, u8 v) override;

 private:
  u64 duration() const { return (adcsr_ & 8) ? 134 : 266; }
  void schedule_conversion(u64 start);
  void cancel_conversion();
  void settle_conversion();
  static void on_conversion(void* self, u64, u64);
  void update_irq();

  Machine& machine_;
  const Source source_;
  u8 adcsr_ = 0;
  u8 adcr_ = 0x7F;
  bool adf_read_ = false;
  u64 conversion_start_ = 0;
  emu::Scheduler::EventId conversion_event_ = 0;
};

}  // namespace h8300h

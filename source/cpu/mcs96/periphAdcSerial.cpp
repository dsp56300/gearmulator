#include "cpu/mcs96/periph.hpp"
#include <algorithm>

namespace mcs96 {
// ---------------------------------------------------------------------------
// A/D

void Peripherals::set_analog_input(unsigned channel, u16 value) {
  if (channel < analog_.size()) analog_[channel] = std::min<u16>(value, 0x03FF);
}

void Peripherals::trigger_adc() {
  if ((adc_command_ & 0x08) == 0) start_adc();
}

void Peripherals::start_adc() {
  const u8 channel = u8(adc_command_ & 0x07);
  adc_sample_ = analog_[channel];
  adc_duration_ = kb() ? ((ioc2_ & 0x10) ? 91 : 158) : 88;
  adc_started_ = p_now();
  adc_busy_ = true;
  adc_result_low_ = u8((adc_result_low_ & 0xC0) | channel);
  reschedule_adc();
}

void Peripherals::finish_adc() {
  const u8 channel = u8(adc_command_ & 0x07);
  adc_result_low_ = u8(channel | ((adc_sample_ & 0x03) << 6));
  adc_result_high_ = u8(adc_sample_ >> 2);
  adc_busy_ = false;
  request(IrqSrc::Adc);
}

void Peripherals::reschedule_adc() {
  cancel(adc_event_);
  if (paused_ || !adc_busy_) return;
  adc_event_ = sched_.schedule(to_cpu_time(adc_started_ + adc_duration_), &Peripherals::on_adc, this);
}

void Peripherals::on_adc(void* self, u64 /*when*/, u64 /*now*/) {
  auto* p = static_cast<Peripherals*>(self);
  p->adc_event_ = 0;
  if (p->adc_busy_) p->finish_adc();
}

// ---------------------------------------------------------------------------
// Serial

u32 Peripherals::serial_bit_period() const {
  if ((baud_rate_ & 0x8000) == 0) return 0;
  const u32 divisor = u32(baud_rate_ & 0x7FFF) + 1;
  const bool asynchronous = (serial_control_ & 3) != 0;
  if (kb()) return divisor * (asynchronous ? 16u : 2u);
  const u32 oscillator_clocks = divisor * (asynchronous ? 64u : 4u);
  return (oscillator_clocks + 2) / 3;
}

void Peripherals::abort_serial() {
  serial_tx_active_ = false;
  serial_tx_pending_ = false;
  cancel(serial_event_);
  serial_status_ |= 0x08;
  serial_tx_line_ = true;
  if (serial_tx_line_hook_) serial_tx_line_hook_(true);
}

void Peripherals::start_serial(u8 value) {
  serial_status_ &= u8(~0x08);
  if (kb() && serial_tx_active_) {
    serial_pending_tx_ = value;
    serial_tx_pending_ = true;
    return;
  }
  serial_tx_ = value;
  serial_tx_active_ = true;
  serial_tx_bit_ = 0;
  const u8 mode = u8(serial_control_ & 3);
  serial_stop_bit_ = mode >= 2 ? 10 : (mode != 0 ? 9 : 8);
  serial_tx_line_ = mode != 0 ? false : (serial_tx_ & 1) != 0;
  if (serial_tx_line_hook_) serial_tx_line_hook_(serial_tx_line_);
  serial_external_count_ = 0;
  reschedule_serial(p_now());
}

// One bit time elapsed (or one external clock period).
void Peripherals::advance_serial() {
  if (!serial_tx_active_) return;
  const u8 mode = u8(serial_control_ & 3);
  ++serial_tx_bit_;
  if (serial_tx_bit_ < serial_stop_bit_) {
    if (mode == 0) serial_tx_line_ = (serial_tx_ & (1u << serial_tx_bit_)) != 0;
    else if (serial_tx_bit_ <= 8 && !(mode == 1 && (serial_control_ & 0x04) != 0 && serial_tx_bit_ == 8))
      serial_tx_line_ = (serial_tx_ & (1u << (serial_tx_bit_ - 1))) != 0;
    else {
      const u8 data_mask = mode == 1 ? 0x7F : 0xFF;
      const bool parity = (serial_control_ & 0x04) != 0 && mode != 2;
      u8 ones = 0;
      for (u8 bits = u8(serial_tx_ & data_mask); bits != 0; bits >>= 1) ones = u8(ones + (bits & 1));
      serial_tx_line_ = parity ? (ones & 1) != 0 : (serial_control_ & 0x10) != 0;
    }
  } else if (serial_tx_bit_ == serial_stop_bit_) {
    serial_tx_line_ = true;
    serial_control_ &= u8(~0x10);
    serial_status_ |= 0x20;
    request(IrqSrc::Serial);
    if (kb()) request(IrqSrc::SerialTransmit);
    if (serial_tx_byte_hook_) serial_tx_byte_hook_(serial_tx_);
  } else if (serial_tx_pending_) {
    serial_tx_ = serial_pending_tx_;
    serial_tx_pending_ = false;
    serial_tx_bit_ = 0;
    serial_stop_bit_ = mode >= 2 ? 10 : (mode != 0 ? 9 : 8);
    serial_tx_line_ = mode != 0 ? false : (serial_tx_ & 1) != 0;
  } else {
    serial_tx_active_ = false;
    serial_status_ |= 0x08;
    return;
  }
  if (serial_tx_line_hook_) serial_tx_line_hook_(serial_tx_line_);
}

void Peripherals::reschedule_serial(u64 from) {
  cancel(serial_event_);
  if (paused_ || !serial_tx_active_) return;
  const u32 period = serial_bit_period();
  if (period == 0) return;  // externally clocked
  serial_event_ = sched_.schedule(to_cpu_time(from + period), &Peripherals::on_serial, this);
}

void Peripherals::on_serial(void* self, u64 when, u64 /*now*/) {
  auto* p = static_cast<Peripherals*>(self);
  p->serial_event_ = 0;
  p->advance_serial();
  // The bit clock keeps its phase across the frame and into a queued byte.
  p->reschedule_serial(when - p->paused_total_);
}

void Peripherals::serial_clock_transition() {
  if (!serial_tx_active_ || (baud_rate_ & 0x8000) != 0) return;
  const u32 divisor = u32(baud_rate_ & 0x7FFF) + 1;
  const u32 threshold = divisor * ((serial_control_ & 3) != 0 ? 8u : 1u);
  if (++serial_external_count_ >= threshold) {
    serial_external_count_ = 0;
    advance_serial();
  }
}

void Peripherals::receive_serial(u16 value) {
  if (kb() && (serial_control_ & 0x08) == 0) return;
  if (serial_rx_full_) serial_status_ |= 0x04;
  serial_rx_ = u8(value);
  serial_rx_full_ = true;
  const u8 mode = u8(serial_control_ & 3);
  if (mode >= 2 && !(mode == 3 && (serial_control_ & 0x04) != 0)) {
    if (value & 0x100) serial_status_ |= 0x80; else serial_status_ &= u8(~0x80);
  }
  if (mode == 2 && (value & 0x100) == 0) return;
  serial_status_ |= 0x40;
  request(IrqSrc::Serial);
  if (kb()) request(IrqSrc::SerialReceive);
}

}

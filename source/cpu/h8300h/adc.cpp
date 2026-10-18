#include "cpu/h8300h/adc.hpp"

#include <utility>

namespace h8300h {

namespace {
constexpr u32 kAdcsr = 0xFFFFE8, kAdcr = 0xFFFFE9;
}  // namespace

Adc::Adc(Machine& machine, Source source) : machine_(machine), source_(std::move(source)) {}

void Adc::reset() {
  cancel_conversion();
  adcsr_ = 0;
  adcr_ = 0x7F;
  adf_read_ = false;
  update_irq();
}

u8 Adc::read8(u32 a) {
  if (a == kAdcsr) {
    settle_conversion();
    if (adcsr_ & 0x80) adf_read_ = true;
    return adcsr_;
  }
  if (a == kAdcr) return adcr_;
  if (a < kBase || a >= kAdcsr) return 0;

  const u32 offset = (a - kBase) & 7;
  const unsigned channel = ((adcsr_ & 4) ? 4u : 0u) | (offset >> 1);
  const int value = source_(channel) & 1023;
  return (offset & 1) ? u8((value << 6) & 0xC0) : u8(value >> 2);
}

void Adc::write8(u32 a, u8 v) {
  if (a == kAdcsr) {
    settle_conversion();
    // ADF clears only when written 0 after having been read as 1
    const bool adf = (adcsr_ & 0x80) && (!adf_read_ || (v & 0x80));
    adcsr_ = u8((v & 0x7F) | (adf ? 0x80 : 0));
    adf_read_ = false;
    cancel_conversion();
    conversion_start_ = machine_.now();
    if ((adcsr_ & 0x60) == 0x60) schedule_conversion(conversion_start_);
    update_irq();
  } else if (a == kAdcr) {
    adcr_ = v | 0x7F;
  }
}

void Adc::schedule_conversion(u64 start) {
  conversion_start_ = start;
  conversion_event_ = machine_.sched().schedule(start + duration(), &on_conversion, this);
}

void Adc::cancel_conversion() {
  if (!conversion_event_) return;
  machine_.sched().cancel(conversion_event_);
  conversion_event_ = 0;
}

// Without ADIE nothing observes a conversion until ADCSR is accessed, so it is resolved then
// instead of waking the scheduler for every conversion of a scan.
void Adc::settle_conversion() {
  if (conversion_event_ || !(adcsr_ & 0x20)) return;
  const u64 now = machine_.now();
  const u64 length = duration();
  if (now < conversion_start_ + length) return;
  adcsr_ |= 0x80;
  if (adcsr_ & 0x10)
    conversion_start_ += (now - conversion_start_) / length * length;
  else
    adcsr_ &= ~0x20;
}

void Adc::on_conversion(void* self, u64, u64) {
  auto* adc = static_cast<Adc*>(self);
  adc->conversion_event_ = 0;
  adc->adcsr_ |= 0x80;
  if (adc->adcsr_ & 0x10)
    adc->schedule_conversion(adc->conversion_start_ + adc->duration());
  else
    adc->adcsr_ &= ~0x20;
  adc->update_irq();
}

void Adc::update_irq() { machine_.periph().set_level(kVecAdi, (adcsr_ & 0xC0) == 0xC0); }

}  // namespace h8300h

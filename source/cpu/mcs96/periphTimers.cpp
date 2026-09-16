#include "cpu/mcs96/periph.hpp"
#include <algorithm>

namespace mcs96 {
namespace { constexpr u32 kTimer1Divider = 8; }
// ---------------------------------------------------------------------------
// TIMER1 / HSO

u32 Peripherals::ticks_to_next_timer1_event() const {
  // The tick that wraps TIMER1 to 0, or the first that lands on a CAM entry.
  u32 best = 0x10000u - timer1_;
  for (const HsoEntry& e : hso_cam_) {
    if (!e.active || (e.command & 0x40)) continue;  // TIMER2-based entries fire on TIMER2 ticks
    u32 d = u32(u16(e.time - timer1_));
    if (d == 0) d = 0x10000;
    best = std::min(best, d);
  }
  return best;
}

void Peripherals::sync_timer1(u64 now) {
  if (now <= timer1_synced_) return;
  const u64 elapsed = now - timer1_synced_;
  timer1_synced_ = now;
  const u64 scaled = timer1_presc_ + elapsed;
  u64 increments = scaled / kTimer1Divider;
  timer1_presc_ = u8(scaled % kTimer1Divider);
  while (increments) {
    const u64 k = std::min<u64>(increments, ticks_to_next_timer1_event());
    timer1_ = u16(timer1_ + k);
    increments -= k;
    if (timer1_ == 0) timer1_overflow();
    process_hso_matches(false);
  }
}

void Peripherals::reschedule_timer1() {
  cancel(timer1_event_);
  if (paused_) return;
  const u64 t = timer1_synced_ + ticks_to_next_timer1_event() * kTimer1Divider - timer1_presc_;
  timer1_event_ = sched_.schedule(to_cpu_time(t), &Peripherals::on_timer1, this);
}

void Peripherals::on_timer1(void* self, u64 /*when*/, u64 /*now*/) {
  auto* p = static_cast<Peripherals*>(self);
  p->timer1_event_ = 0;
  p->sync_timer1(p->p_now());
  p->reschedule_timer1();
}

void Peripherals::timer1_overflow() {
  ios1_ |= 0x20;
  if (ioc1_ & 0x04) request(IrqSrc::Timer);
}

void Peripherals::timer2_overflow() {
  ios1_ |= 0x10;
  if (ioc1_ & 0x08) request(IrqSrc::Timer);
  if (kb()) request(IrqSrc::Timer2Overflow);
}

bool Peripherals::timer2_counts_down() const { return kb() && (ioc2_ & 0x02) != 0 && timer2_up_down_input_; }

void Peripherals::timer2_clock_transition() {
  const u16 old = timer2_;
  if (timer2_counts_down()) {
    --timer2_;
    if (((ioc2_ & 0x20) != 0 && old == 0x8000) || ((ioc2_ & 0x20) == 0 && old == 0)) timer2_overflow();
  } else {
    ++timer2_;
    if (((ioc2_ & 0x20) != 0 && old == 0x7FFF) || ((ioc2_ & 0x20) == 0 && old == 0xFFFF)) timer2_overflow();
  }
  process_hso_matches(true);
}

void Peripherals::timer2_reset_event() {
  timer2_ = 0;
  process_hso_matches(true);
}

void Peripherals::write_ioc0(u8 value) {
  if (value & 0x02) timer2_reset_event();
  ioc0_ = u8(value & ~0x02);
}

void Peripherals::set_timer2_clock_input(bool level) {
  if (timer2_clock_input_ == level) return;
  timer2_clock_input_ = level;
  if (!cpu_.power_down() && (ioc0_ & 0x80) == 0) timer2_clock_transition();
  if (level) serial_clock_transition();
}

void Peripherals::set_timer2_reset_input(bool level) {
  if (!cpu_.power_down() && !timer2_reset_input_ && level && (ioc0_ & 0x28) == 0x08) timer2_reset_event();
  timer2_reset_input_ = level;
}

void Peripherals::set_timer2_up_down_input(bool level) {
  timer2_up_down_input_ = level;
  if (level) port_input_[2] |= 0x40; else port_input_[2] &= u8(~0x40);
}

void Peripherals::set_timer2_capture_input(bool level) {
  if (kb() && !cpu_.power_down() && !timer2_capture_input_ && level) {
    timer2_capture_ = timer2_;
    request(IrqSrc::Timer2Capture);
  }
  timer2_capture_input_ = level;
}

void Peripherals::commit_hso_cam() {
  for (HsoEntry& entry : hso_cam_) {
    if (entry.active) continue;
    entry = {hso_time_, hso_command_, true};
    bool full = true;
    for (const HsoEntry& candidate : hso_cam_) full = full && candidate.active;
    if (full) ios0_ |= 0x40;
    return;
  }
  hso_holding_ = {hso_time_, hso_command_, true};
  hso_holding_valid_ = true;
  ios0_ |= 0xC0;
}

void Peripherals::clear_hso_cam() {
  for (HsoEntry& entry : hso_cam_) entry = {};
  hso_holding_ = {};
  hso_holding_valid_ = false;
  ios0_ &= 0x3F;
}

void Peripherals::process_hso_matches(bool timer2) {
  if (processing_hso_) return;
  processing_hso_ = true;
  std::array<bool, 8> matches{};
  for (unsigned i = 0; i < hso_cam_.size(); ++i)
    matches[i] = hso_cam_[i].active && (((hso_cam_[i].command & 0x40) != 0) == timer2) &&
                 hso_cam_[i].time == (timer2 ? timer2_ : timer1_);
  for (unsigned i = 0; i < matches.size(); ++i)
    if (matches[i]) trigger_hso(i);
  processing_hso_ = false;
}

void Peripherals::trigger_hso(unsigned slot) {
  if (slot >= hso_cam_.size() || !hso_cam_[slot].active) return;
  const u8 command = hso_cam_[slot].command;
  const bool locked = kb() && (command & 0x80) != 0 && (ioc2_ & 0x40) != 0;
  if (!locked) {
    hso_cam_[slot].active = false;
    ios0_ &= u8(~0x40);
  }
  const u8 action = u8(command & 0x0F);
  if (action <= 5) {
    if (kb()) ios2_ |= u8(1u << action);
    set_hso_output(u8(1u << action), (command & 0x20) != 0);
  } else if (action == 6 || action == 7) {
    const u8 mask = action == 6 ? 0x03 : 0x0C;
    if (kb()) ios2_ |= mask;
    set_hso_output(mask, (command & 0x20) != 0);
  } else if (action >= 8 && action <= 11) {
    ios1_ |= u8(1u << (action & 3));
  } else if (action == 14) {
    if (kb()) ios2_ |= 0x40;
    timer2_reset_event();
  } else if (action == 15) {
    if (kb()) ios2_ |= 0x80;
    start_adc();
  }
  if (command & 0x10) request((command & 0x08) ? IrqSrc::SoftwareTimer : IrqSrc::Hso);
  if (!locked) transfer_hso_holding();
}

void Peripherals::set_hso_output(u8 mask, bool state) {
  if (state) ios0_ |= u8(mask & 0x3F); else ios0_ &= u8(~(mask & 0x3F));
  if (hso_hook_) hso_hook_(u8(ios0_ & 0x3F));
}

void Peripherals::transfer_hso_holding() {
  if (!hso_holding_valid_) return;
  for (HsoEntry& entry : hso_cam_) {
    if (entry.active) continue;
    entry = hso_holding_;
    entry.active = true;
    hso_holding_ = {};
    hso_holding_valid_ = false;
    ios0_ &= u8(~0x80);
    bool full = true;
    for (const HsoEntry& candidate : hso_cam_) full = full && candidate.active;
    if (full) ios0_ |= 0x40;
    return;
  }
}

// ---------------------------------------------------------------------------
// HSI

void Peripherals::set_hsi_input(unsigned pin, bool level) {
  if (pin >= hsi_input_.size() || hsi_input_[pin] == level) return;
  hsi_input_[pin] = level;
  const u8 state_bit = u8(2u << (pin * 2));
  if (level) hsi_status_ |= state_bit; else hsi_status_ &= u8(~state_bit);
  if (cpu_.power_down()) return;
  if (pin == 0 && level) {
    request(IrqSrc::Hsi0);
    if ((ioc0_ & 0x28) == 0x28) timer2_reset_event();
  }
  if (pin == 1 && (ioc0_ & 0x80)) timer2_clock_transition();
  if ((ioc0_ & (1u << (pin * 2))) == 0) return;
  const u8 mode = u8((hsi_mode_ >> (pin * 2)) & 3);
  bool capture = (mode == 1 && level) || (mode == 2 && !level) || mode == 3;
  if (mode == 0 && level) {
    hsi_transition_count_[pin] = u8((hsi_transition_count_[pin] + 1) & 7);
    capture = hsi_transition_count_[pin] == 0;
  }
  if (capture) push_hsi(pin);
}

void Peripherals::push_hsi(unsigned pin) {
  sync_timer1(p_now());
  const u8 event = u8(1u << (pin * 2));
  if (hsi_count_ != 0 && hsi_fifo_[hsi_count_ - 1].time == timer1_) {
    hsi_fifo_[hsi_count_ - 1].events |= event;
    update_hsi_status();
    return;
  }
  if (hsi_count_ == hsi_fifo_.size()) return;
  const u8 old_count = hsi_count_;
  hsi_fifo_[hsi_count_++] = {timer1_, event};
  update_hsi_status();
  if (old_count == 0 && (ioc1_ & 0x80) == 0) request(IrqSrc::HsiData);
  if (kb() && old_count < 5 && hsi_count_ >= 5) request(IrqSrc::HsiFifoFour);
  if (old_count < 7 && hsi_count_ >= 7) {
    if (kb()) request(IrqSrc::HsiFifoFull);
    if (ioc1_ & 0x80) request(IrqSrc::HsiData);
  }
}

void Peripherals::pop_hsi() {
  if (hsi_count_ == 0) return;
  const bool refill = hsi_count_ > 1;
  for (unsigned i = 1; i < hsi_count_; ++i) hsi_fifo_[i - 1] = hsi_fifo_[i];
  --hsi_count_;
  update_hsi_status();
  if (refill && (ioc1_ & 0x80) == 0) request(IrqSrc::HsiData);
}

void Peripherals::update_hsi_status() {
  hsi_status_ = u8((hsi_status_ & 0xAA) | (hsi_count_ != 0 ? hsi_fifo_[0].events : 0));
  if (hsi_count_ != 0) ios1_ |= 0x80; else ios1_ &= u8(~0x80);
  if (hsi_count_ >= 7) ios1_ |= 0x40; else ios1_ &= u8(~0x40);
}

u8 Peripherals::read_hsi_time(u8 address) {
  if (!hsi_read_latch_valid_) {
    if (hsi_count_ == 0) return 0xFF;
    hsi_read_latch_ = hsi_fifo_[0].time;
    hsi_read_latch_valid_ = true;
  }
  if (address == 0x04) return u8(hsi_read_latch_);
  const u8 value = u8(hsi_read_latch_ >> 8);
  hsi_read_latch_valid_ = false;
  pop_hsi();
  return value;
}

// ---------------------------------------------------------------------------
// PWM

void Peripherals::sync_pwm(u64 now) {
  if (now <= pwm_synced_) return;
  const u64 elapsed = now - pwm_synced_;
  pwm_synced_ = now;
  const unsigned divider = pwm_divider();
  const u64 scaled = pwm_presc_ + elapsed;
  const u64 increments = scaled / divider;
  pwm_presc_ = u8(scaled % divider);
  if (!increments) return;
  // Each wrap of the 8-bit counter reloads the duty from the shadow and
  // raises the output; reaching the duty drops it.  Only the last wrap
  // matters for the state; edges in between reach the hook through events.
  const u64 begin = pwm_counter_;
  const u64 end = begin + increments;
  pwm_counter_ = u8(end);
  if (end >= 0x100) {
    pwm_active_ = pwm_shadow_;
    pwm_output_ = pwm_active_ != 0 && pwm_counter_ < pwm_active_;
  } else if (pwm_active_ > begin && pwm_active_ <= end) {
    pwm_output_ = false;
  }
}

void Peripherals::reschedule_pwm() {
  cancel(pwm_event_);
  if (paused_) return;
  // Without a listener only the wrap that latches a new duty value is worth
  // an event, and only while one is pending.
  const unsigned divider = pwm_divider();
  u64 ticks;
  if (pwm_hook_) {
    const u32 to_wrap = 0x100u - pwm_counter_;
    const u32 to_duty = pwm_active_ > pwm_counter_ ? pwm_active_ - pwm_counter_ : to_wrap;
    ticks = std::min(to_wrap, to_duty);
  } else if (pwm_shadow_ != pwm_active_) {
    ticks = 0x100u - pwm_counter_;
  } else {
    return;
  }
  pwm_event_ = sched_.schedule(to_cpu_time(pwm_synced_ + ticks * divider - pwm_presc_), &Peripherals::on_pwm, this);
}

void Peripherals::on_pwm(void* self, u64 /*when*/, u64 /*now*/) {
  auto* p = static_cast<Peripherals*>(self);
  p->pwm_event_ = 0;
  const bool before = p->pwm_output_;
  p->sync_pwm(p->p_now());
  if (p->pwm_hook_ && before != p->pwm_output_) p->pwm_hook_(p->pwm_output_);
  p->reschedule_pwm();
}

// ---------------------------------------------------------------------------
// Watchdog (KB)

u32 Peripherals::watchdog_counter() const {
  if (!watchdog_enabled_) return 0;
  return u32(std::min<u64>(p_now() - watchdog_started_, 0xFFFF));
}

void Peripherals::reschedule_watchdog() {
  cancel(watchdog_event_);
  if (paused_ || !kb() || !watchdog_enabled_) return;
  watchdog_event_ = sched_.schedule(to_cpu_time(watchdog_started_ + 0x10000), &Peripherals::on_watchdog, this);
}

void Peripherals::on_watchdog(void* self, u64 /*when*/, u64 /*now*/) {
  auto* p = static_cast<Peripherals*>(self);
  p->watchdog_event_ = 0;
  if (!p->watchdog_enabled_) return;
  if (p->reset_sink_) p->reset_sink_->watchdog_reset();
}

}

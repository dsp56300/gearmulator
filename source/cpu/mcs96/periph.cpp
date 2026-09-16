#include "cpu/mcs96/periph.hpp"

#include <algorithm>

namespace mcs96 {

namespace {
constexpr u32 kTimer1Divider = 8;
}  // namespace

Peripherals::Peripherals(emu::Scheduler& sched, Cpu& cpu) : sched_(sched), cpu_(cpu) {
  cpu_.set_sfr_block(this);
}

Peripherals::~Peripherals() {
  cancel(timer1_event_);
  cancel(adc_event_);
  cancel(serial_event_);
  cancel(pwm_event_);
  cancel(watchdog_event_);
  cpu_.set_sfr_block(nullptr);
}

void Peripherals::reset_sfrs() {
  cancel(timer1_event_);
  cancel(adc_event_);
  cancel(serial_event_);
  cancel(pwm_event_);
  cancel(watchdog_event_);
  paused_ = false;
  paused_since_ = 0;
  paused_total_ = 0;
  const u64 now = p_now();

  pending_low_ = pending_high_ = mask_high_ = 0;
  nmi_pending_ = nmi_input_ = false;
  external_input_.fill(false);
  hsi_input_.fill(false);
  hsi_transition_count_.fill(0);
  timer2_clock_input_ = timer2_reset_input_ = timer2_up_down_input_ = timer2_capture_input_ = false;

  timer1_ = timer2_ = timer2_capture_ = 0;
  timer1_presc_ = 0;
  timer1_synced_ = now;

  ioc0_ = 0;
  ioc1_ = kb() ? 0x21 : 0x01;
  ioc2_ = ios0_ = ios1_ = ios2_ = 0;

  port_input_.fill(0xFF);
  port_latch_.fill(0xFF);
  port_latch_[2] = 0xC1;
  if (port_out_[1]) port_out_[1](port_latch_[1]);
  if (port_out_[2]) port_out_[2](port_latch_[2]);
  analog_.fill(0);

  adc_command_ = 0;
  adc_result_low_ = kb() ? 0xF0 : 0;
  adc_result_high_ = kb() ? 0x7F : 0;
  adc_sample_ = 0;
  adc_started_ = 0;
  adc_duration_ = 0;
  adc_busy_ = false;

  pwm_shadow_ = pwm_active_ = pwm_counter_ = pwm_presc_ = 0;
  pwm_synced_ = now;
  pwm_output_ = false;

  window_select_ = window_control_ = program_pulse_width_ = 0;
  hsi_mode_ = kb() ? 0xFF : 0;
  hsi_fifo_.fill({});
  hsi_count_ = 0;
  hsi_read_latch_ = 0;
  hsi_read_latch_valid_ = false;
  hsi_status_ = 0;
  hso_time_ = 0;
  hso_command_ = 0;
  hso_cam_.fill({});
  hso_holding_ = {};
  hso_holding_valid_ = false;
  processing_hso_ = false;

  serial_rx_ = serial_tx_ = serial_pending_tx_ = 0;
  serial_control_ = kb() ? 0x0B : 0;
  serial_status_ = kb() ? 0x08 : 0;
  baud_rate_ = 0;
  baud_high_byte_ = false;
  serial_rx_full_ = serial_tx_active_ = serial_tx_pending_ = false;
  serial_tx_line_ = true;
  serial_tx_bit_ = serial_stop_bit_ = 0;
  serial_external_count_ = 0;

  watchdog_ = watchdog_key_ = 0;
  watchdog_enabled_ = false;
  watchdog_started_ = 0;

  if (serial_tx_line_hook_) serial_tx_line_hook_(true);
  if (pwm_hook_) pwm_hook_(false);
  if (hso_hook_) hso_hook_(0);
  reschedule_all();
  update_irq();
}

void Peripherals::reschedule_all() {
  reschedule_timer1();
  reschedule_adc();
  reschedule_serial(p_now());
  reschedule_pwm();
  reschedule_watchdog();
}

// ---------------------------------------------------------------------------
// Power-down: the block's clock stops

void Peripherals::on_power_down(bool entering) {
  if (entering) {
    if (paused_) return;
    sync_timer1(p_now());
    sync_pwm(p_now());
    paused_ = true;
    paused_since_ = cpu_.now();
    // A conversion in flight is abandoned.
    adc_busy_ = false;
    adc_result_low_ &= u8(~0x08);
    cancel(adc_event_);
    cancel(timer1_event_);
    cancel(serial_event_);
    cancel(pwm_event_);
    cancel(watchdog_event_);
    return;
  }
  if (!paused_) return;
  paused_total_ += cpu_.now() - paused_since_;
  paused_ = false;
  reschedule_all();
}

// ---------------------------------------------------------------------------
// SFR window

u8 Peripherals::read_sfr(u8 address) {
  if (kb() && address == 0x14) return u8(window_select_ | window_control_);
  if (!kb() || window_select_ == 0) return read_window0(address);
  if (window_select_ == 15) return read_window15(address);
  if (window_select_ == 14 && address == 0x04) return program_pulse_width_;
  return 0xFF;
}

void Peripherals::write_sfr(u8 address, u8 value) {
  if (kb() && address == 0x14) {
    const u8 window = u8(value & 0x0F);
    if (window == 0 || window == 14 || window == 15) {
      window_select_ = window;
      window_control_ = u8(value & 0xF0);
    }
    return;
  }
  if (!kb() || window_select_ == 0) write_window0(address, value);
  else if (window_select_ == 15) write_window15(address, value);
  else if (window_select_ == 14 && address == 0x04) program_pulse_width_ = value;
}

u8 Peripherals::read_window0(u8 address) {
  switch (address) {
    case 0x02: {
      const bool sampling = adc_busy_ && (p_now() - adc_started_) >= 8;
      return u8((adc_result_low_ & ~0x08) | (sampling ? 0x08 : 0));
    }
    case 0x03: return adc_result_high_;
    case 0x04: case 0x05: return read_hsi_time(address);
    case 0x06: return hsi_status_;
    case 0x07: serial_rx_full_ = false; return serial_rx_;
    case 0x09: return pending_low_;
    case 0x0A: return u8(timer1());
    case 0x0B: return u8(timer1() >> 8);
    case 0x0C: return u8(timer2_);
    case 0x0D: return u8(timer2_ >> 8);
    case 0x0E: return read_port(0);
    case 0x0F: return read_port(1);
    case 0x10: return read_port(2);
    case 0x11: {
      const u8 value = serial_status_;
      serial_status_ &= u8(kb() ? 0x88 : 0x80);
      return value;
    }
    case 0x12: return kb() ? pending_high_ : u8(0xFF);
    case 0x13: return kb() ? u8(mask_high_ & 0x7F) : u8(0xFF);
    case 0x15: return ios0_;
    case 0x16: {
      const u8 value = ios1_;
      ios1_ &= 0xC0;
      return value;
    }
    case 0x17:
      if (kb()) { const u8 value = ios2_; ios2_ = 0; return value; }
      return 0xFF;
    default: return 0xFF;
  }
}

void Peripherals::write_window0(u8 address, u8 value) {
  switch (address) {
    case 0x02:
      adc_command_ = u8(value & 0x0F);
      if (value & 0x08) start_adc();
      break;
    case 0x03: hsi_mode_ = value; break;
    case 0x04: hso_time_ = u16((hso_time_ & 0xFF00) | value); break;
    case 0x05:
      hso_time_ = u16((hso_time_ & 0x00FF) | (u16(value) << 8));
      sync_timer1(p_now());
      commit_hso_cam();
      reschedule_timer1();
      break;
    case 0x06: hso_command_ = value; break;
    case 0x07: start_serial(value); break;
    case 0x09: pending_low_ = value; update_irq(); break;
    case 0x0A:
      watchdog_ = value;
      if (kb()) {
        if (watchdog_key_ == 0 && value == 0x1E) watchdog_key_ = 1;
        else if (watchdog_key_ == 1 && value == 0xE1) {
          watchdog_enabled_ = true;
          watchdog_started_ = p_now();
          watchdog_key_ = 0;
          reschedule_watchdog();
        } else watchdog_key_ = 0;
      }
      break;
    case 0x0B:
      if (kb()) {
        if (value & 0x80) clear_hso_cam();
        sync_pwm(p_now());
        ioc2_ = u8(value & 0x7F);
        reschedule_pwm();
        reschedule_timer1();
      }
      break;
    case 0x0C: if (kb()) timer2_ = u16((timer2_ & 0xFF00) | value); break;
    case 0x0D:
      if (kb()) {
        timer2_ = u16((timer2_ & 0x00FF) | (u16(value) << 8));
        process_hso_matches(true);
      }
      break;
    case 0x0E:
      if (!baud_high_byte_) baud_rate_ = u16((baud_rate_ & 0xFF00) | value);
      else baud_rate_ = u16((baud_rate_ & 0x00FF) | (u16(value) << 8));
      baud_high_byte_ = !baud_high_byte_;
      break;
    case 0x0F:
      port_latch_[1] = value;
      if (port_out_[1]) port_out_[1](port_latch_[1]);
      break;
    case 0x10:
      port_latch_[2] = u8(value & 0xE1);
      if (port_out_[2]) port_out_[2](port_latch_[2]);
      break;
    case 0x11:
      if ((serial_control_ ^ value) & 3) abort_serial();
      serial_control_ = u8(value & 0x1F);
      break;
    case 0x12: if (kb()) { pending_high_ = value; update_irq(); } break;
    case 0x13: if (kb()) { mask_high_ = u8(value & 0x7F); update_irq(); } break;
    case 0x15: write_ioc0(value); break;
    case 0x16: ioc1_ = value; update_irq(); break;
    case 0x17: sync_pwm(p_now()); pwm_shadow_ = value; reschedule_pwm(); break;
    default: break;
  }
}

u8 Peripherals::read_window15(u8 address) {
  switch (address) {
    case 0x02: return adc_command_;
    case 0x03: return hsi_mode_;
    case 0x04: return u8(hso_time_);
    case 0x05: return u8(hso_time_ >> 8);
    case 0x06: return hso_command_;
    case 0x07: return serial_tx_;
    case 0x0A: return u8(watchdog_counter() >> 8);
    case 0x0B: return u8(ioc2_ | 0x80);
    case 0x0C: return u8(timer2_capture_);
    case 0x0D: return u8(timer2_capture_ >> 8);
    case 0x11: return serial_control_;
    case 0x15: return u8(ioc0_ | 0x02);
    case 0x16: return ioc1_;
    case 0x17: return pwm_shadow_;
    default: return 0xFF;
  }
}

void Peripherals::write_window15(u8 address, u8 value) {
  switch (address) {
    case 0x02: adc_result_low_ = value; break;
    case 0x03: adc_result_high_ = value; break;
    case 0x04:
      if (hsi_count_ == 0) { hsi_count_ = 1; hsi_fifo_[0] = {}; }
      hsi_fifo_[0].time = u16((hsi_fifo_[0].time & 0xFF00) | value);
      update_hsi_status();
      break;
    case 0x05:
      if (hsi_count_ == 0) { hsi_count_ = 1; hsi_fifo_[0] = {}; }
      hsi_fifo_[0].time = u16((hsi_fifo_[0].time & 0x00FF) | (u16(value) << 8));
      update_hsi_status();
      break;
    case 0x06:
      if (hsi_count_ == 0) { hsi_count_ = 1; hsi_fifo_[0] = {}; }
      hsi_fifo_[0].events = u8(value & 0x55);
      update_hsi_status();
      break;
    case 0x07: serial_rx_ = value; break;
    case 0x0A:
      sync_timer1(p_now());
      timer1_ = u16((timer1_ & 0xFF00) | value);
      timer1_presc_ = 0;
      reschedule_timer1();
      break;
    case 0x0B:
      sync_timer1(p_now());
      timer1_ = u16((timer1_ & 0x00FF) | (u16(value) << 8));
      timer1_presc_ = 0;
      reschedule_timer1();
      break;
    case 0x0C: timer2_capture_ = u16((timer2_capture_ & 0xFF00) | value); break;
    case 0x0D: timer2_capture_ = u16((timer2_capture_ & 0x00FF) | (u16(value) << 8)); break;
    case 0x11: serial_status_ |= u8(value & 0xFC); break;
    case 0x15: {
      const u8 changed = u8((ios0_ ^ value) & 0x3F);
      set_hso_output(u8(changed & ~value), false);
      set_hso_output(u8(changed & value), true);
      break;
    }
    case 0x16: ios1_ |= u8(value & 0x3F); break;
    case 0x17: ios2_ |= value; break;
    default: break;
  }
}

u16 Peripherals::aux_psw() const {
  return kb() ? u16(mask_high_ | (u16(window_select_ | window_control_) << 8)) : u16(0);
}

void Peripherals::set_aux_psw(u16 value) {
  if (!kb()) return;
  mask_high_ = u8(value & 0x7F);
  const u8 wsr = u8(value >> 8);
  const u8 window = u8(wsr & 0x0F);
  if (window == 0 || window == 14 || window == 15) {
    window_select_ = window;
    window_control_ = u8(wsr & 0xF0);
  }
  update_irq();
}

// ---------------------------------------------------------------------------
// Interrupts

bool Peripherals::arbitrate(Interrupt& out) const {
  if (!kb() && nmi_pending_) {
    out = {0x0000, false, false, u8(IrqSrc::Nmi)};
    return true;
  }
  if (kb()) {
    for (int source = 15; source >= 8; --source) {
      const u8 bit = u8(1u << (source - 8));
      const bool enabled = source == 15 || (mask_high_ & bit) != 0;
      if ((pending_high_ & bit) != 0 && enabled) {
        out = {u16(0x2030 + (source - 8) * 2), source != 15, true, u8(source)};
        return true;
      }
    }
  }
  const u8 enabled_low = u8(pending_low_ & cpu_.regs().psw);
  for (int source = 7; source >= 0; --source) {
    const u8 bit = u8(1u << source);
    if (enabled_low & bit) {
      out = {u16(0x2000 + source * 2), true, true, u8(source)};
      return true;
    }
  }
  return false;
}

void Peripherals::acknowledge(const Interrupt& irq) {
  if (irq.source < 8) pending_low_ &= u8(~(1u << irq.source));
  else if (irq.source < 16 && kb()) pending_high_ &= u8(~(1u << (irq.source - 8)));
  else if (irq.source == u8(IrqSrc::Nmi)) nmi_pending_ = false;
  update_irq();
}

void Peripherals::request(IrqSrc source) {
  const u8 s = u8(source);
  if (s < 8) pending_low_ |= u8(1u << s);
  else if (kb()) pending_high_ |= u8(1u << (s - 8));
  else if (source == IrqSrc::Nmi) nmi_pending_ = true;
  update_irq();
}

void Peripherals::set_external_interrupt_input(unsigned line, bool level) {
  if (line >= external_input_.size()) return;
  const bool rising = !external_input_[line] && level;
  external_input_[line] = level;
  if (!rising) return;
  if (!kb()) {
    if (line == 0 && (ioc1_ & 0x02) == 0) request(IrqSrc::External0);
    return;
  }
  if (line == 0) {
    if (ioc1_ & 0x02) {
      cpu_.wake();
      request(IrqSrc::External0);
    }
  } else {
    if (cpu_.power_down() && (ioc1_ & 0x02)) return;
    cpu_.wake();
    request(IrqSrc::External1);
    if ((ioc1_ & 0x02) == 0) request(IrqSrc::External0);
  }
}

void Peripherals::set_nmi_input(bool level) {
  if (!cpu_.power_down() && !nmi_input_ && level) {
    if (kb()) request(IrqSrc::Nmi);
    else { nmi_pending_ = true; update_irq(); }
  }
  nmi_input_ = level;
}

// ---------------------------------------------------------------------------
// Ports

u8 Peripherals::read_port(unsigned port) const {
  if (port == 0) return port_input_[0];
  if (port == 2 && !kb()) {
    // On the NMOS 8x9x, EXTINT is multiplexed onto the P2.2 readback
    // independently of the output latch.
    const u8 multiplexed = external_input_[0] ? 0x1E : 0x1A;
    return u8((port_input_[2] | 0x25) & (port_latch_[2] | multiplexed));
  }
  if (port == 2) return u8((port_latch_[2] & 0x21) | (port_input_[2] & 0x1E) | (port_input_[2] & port_latch_[2] & 0xC0));
  return u8(port_input_[port] & port_latch_[port]);
}

void Peripherals::set_port_input(unsigned port, u8 value) {
  if (port >= port_input_.size()) return;
  if (kb() && port == 2 && (port_input_[2] & 0x80) == 0 && (value & 0x80) != 0) {
    timer2_capture_ = timer2_;
    request(IrqSrc::Timer2Capture);
  }
  port_input_[port] = value;
  if (port == 2) timer2_up_down_input_ = (value & 0x40) != 0;
}

}  // namespace mcs96

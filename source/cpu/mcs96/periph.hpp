// On-chip peripheral block of the 8x9x / 80C196KB (user's manuals, chapters
// on the timers, HSI/HSO units, A/D converter, serial port, PWM, ports,
// watchdog and interrupt controller).
//
// The block sits behind the SFR window (02h-17h of the register file; the KB
// switches three windows through WSR at 14h) and owns the interrupt pending /
// mask logic the CPU arbitrates through SfrBlock.  Time-based units are lazy
// models on the state clock: TIMER1 (one increment per eight states) with the
// HSO content-addressable memory that fires on its value, the A/D conversion,
// the serial transmitter bit clock, the PWM counter and the KB watchdog; each
// keeps one scheduler event for its next observable transition.  TIMER2 and
// the HSI unit are driven by the board's pin transitions.
//
// Entering power-down (IDLPD #2) freezes the block: its clock stops advancing
// until an external interrupt pin wakes the core.
#pragma once
#include <array>
#include <functional>

#include "common/sched.hpp"
#include "cpu/mcs96/cpu.hpp"

namespace mcs96 {

class ResetSink {
 public:
  virtual ~ResetSink() = default;
  virtual void watchdog_reset() = 0;
};

class Peripherals final : public SfrBlock {
 public:
  enum class IrqSrc : u8 {
    Timer = 0, Adc = 1, HsiData = 2, Hso = 3, Hsi0 = 4, SoftwareTimer = 5, Serial = 6, External0 = 7,
    // 80C196KB only
    SerialTransmit = 8, SerialReceive = 9, HsiFifoFour = 10, Timer2Capture = 11, Timer2Overflow = 12,
    External1 = 13, HsiFifoFull = 14, Nmi = 15,
  };
  using LineHook = std::function<void(bool)>;
  using ByteHook = std::function<void(u8)>;

  Peripherals(emu::Scheduler& sched, Cpu& cpu);
  ~Peripherals() override;

  void set_reset_sink(ResetSink* s) { reset_sink_ = s; }

  // --- SfrBlock ---------------------------------------------------------------
  u8 read_sfr(u8 address) override;
  void write_sfr(u8 address, u8 value) override;
  void reset_sfrs() override;
  u16 aux_psw() const override;
  void set_aux_psw(u16 value) override;
  bool arbitrate(Interrupt& out) const override;
  void acknowledge(const Interrupt& irq) override;
  void on_power_down(bool entering) override;

  // --- module side --------------------------------------------------------------
  void request(IrqSrc source);
  u8 interrupt_pending_low() const { return pending_low_; }
  u8 interrupt_pending_high() const { return pending_high_; }

  // --- pins (board side) ----------------------------------------------------------
  void set_external_interrupt_input(unsigned line, bool level);
  void set_nmi_input(bool level);
  void set_hsi_input(unsigned pin, bool level);
  void set_timer2_clock_input(bool level);
  void set_timer2_reset_input(bool level);
  void set_timer2_up_down_input(bool level);
  void set_timer2_capture_input(bool level);
  // TIMER2 counts both edges of its selected source; the board calls this
  // once per selected transition.
  void timer2_clock_transition();
  void timer2_reset_event();
  // External serial clock (baud rate register bit 15 clear).
  void serial_clock_transition();
  // A received frame (bit 8 = the ninth data bit in modes 2/3).
  void receive_serial(u16 value);

  void set_port_input(unsigned port, u8 value);
  u8 port_input(unsigned port) const { return port < 3 ? port_input_[port] : u8(0xFF); }
  u8 port_output(unsigned port) const { return port < 3 ? port_latch_[port] : u8(0xFF); }
  void set_port_output_hook(unsigned port, ByteHook h) { if (port < 3) port_out_[port] = std::move(h); }

  void set_analog_input(unsigned channel, u16 value);
  u16 analog_input(unsigned channel) const { return channel < 8 ? analog_[channel] : 0; }
  bool adc_busy() const { return adc_busy_; }
  void trigger_adc();

  u16 timer1() { sync_timer1(p_now()); return timer1_; }
  u16 timer2() const { return timer2_; }
  u16 timer2_capture() const { return timer2_capture_; }
  u8 hso_output() const { return u8(ios0_ & 0x3F); }
  unsigned hsi_fifo_count() const { return hsi_count_; }
  bool serial_tx_active() const { return serial_tx_active_; }
  bool serial_tx_line() const { return serial_tx_line_; }
  bool watchdog_enabled() const { return watchdog_enabled_; }
  void set_serial_tx_line_hook(LineHook h) { serial_tx_line_hook_ = std::move(h); }
  void set_serial_tx_byte_hook(ByteHook h) { serial_tx_byte_hook_ = std::move(h); }
  void set_pwm_hook(LineHook h) { pwm_hook_ = std::move(h); reschedule_pwm(); }
  void set_hso_hook(ByteHook h) { hso_hook_ = std::move(h); }
  bool pwm_output() { sync_pwm(p_now()); return (ioc1_ & 0x01) != 0 && pwm_output_; }
  u8 pwm_counter() { sync_pwm(p_now()); return pwm_counter_; }
  u8 pwm_duty() { sync_pwm(p_now()); return pwm_active_; }

 private:
  bool kb() const { return cpu_.variant() == Variant::I80C196KB; }
  // Peripheral time: the state clock, stopped while powered down.
  u64 p_now() const { return (paused_ ? paused_since_ : cpu_.now()) - paused_total_; }
  u64 to_cpu_time(u64 p) const { return p + paused_total_; }
  void update_irq() { cpu_.reeval_irq(); }

  // windows
  u8 read_window0(u8 address);
  void write_window0(u8 address, u8 value);
  u8 read_window15(u8 address);
  void write_window15(u8 address, u8 value);
  u8 read_port(unsigned port) const;
  void write_ioc0(u8 value);

  // timer 1 / HSO
  static void on_timer1(void* self, u64 when, u64 now);
  void sync_timer1(u64 now);
  void reschedule_timer1();
  u32 ticks_to_next_timer1_event() const;
  void timer1_overflow();
  void timer2_overflow();
  bool timer2_counts_down() const;
  void commit_hso_cam();
  void clear_hso_cam();
  void process_hso_matches(bool timer2);
  void trigger_hso(unsigned slot);
  void set_hso_output(u8 mask, bool state);
  void transfer_hso_holding();

  // HSI
  void push_hsi(unsigned pin);
  void pop_hsi();
  void update_hsi_status();
  u8 read_hsi_time(u8 address);

  // A/D
  static void on_adc(void* self, u64 when, u64 now);
  void start_adc();
  void finish_adc();
  void reschedule_adc();

  // serial
  static void on_serial(void* self, u64 when, u64 now);
  u32 serial_bit_period() const;
  void start_serial(u8 value);
  void advance_serial();
  void reschedule_serial(u64 from);
  void abort_serial();

  // PWM
  static void on_pwm(void* self, u64 when, u64 now);
  void sync_pwm(u64 now);
  void reschedule_pwm();
  unsigned pwm_divider() const { return kb() && (ioc2_ & 0x04) != 0 ? 2 : 1; }

  // watchdog
  static void on_watchdog(void* self, u64 when, u64 now);
  void reschedule_watchdog();
  u32 watchdog_counter() const;

  void reschedule_all();
  void cancel(emu::Scheduler::EventId& e) { if (e) { sched_.cancel(e); e = 0; } }

  emu::Scheduler& sched_;
  Cpu& cpu_;
  ResetSink* reset_sink_ = nullptr;
  bool paused_ = false;
  u64 paused_since_ = 0, paused_total_ = 0;
  emu::Scheduler::EventId timer1_event_ = 0, adc_event_ = 0, serial_event_ = 0, pwm_event_ = 0, watchdog_event_ = 0;

  // interrupts
  u8 pending_low_ = 0, pending_high_ = 0, mask_high_ = 0;
  bool nmi_pending_ = false, nmi_input_ = false;
  std::array<bool, 2> external_input_{};

  // timers
  u16 timer1_ = 0, timer2_ = 0, timer2_capture_ = 0;
  u8 timer1_presc_ = 0;
  u64 timer1_synced_ = 0;
  bool timer2_clock_input_ = false, timer2_reset_input_ = false, timer2_up_down_input_ = false, timer2_capture_input_ = false;

  u8 ioc0_ = 0, ioc1_ = 0, ioc2_ = 0, ios0_ = 0, ios1_ = 0, ios2_ = 0;

  // ports
  std::array<u8, 3> port_input_{}, port_latch_{};
  std::array<ByteHook, 3> port_out_{};

  // A/D
  std::array<u16, 8> analog_{};
  u8 adc_command_ = 0, adc_result_low_ = 0, adc_result_high_ = 0;
  u16 adc_sample_ = 0;
  u64 adc_started_ = 0;
  u32 adc_duration_ = 0;
  bool adc_busy_ = false;

  // PWM
  u8 pwm_shadow_ = 0, pwm_active_ = 0, pwm_counter_ = 0, pwm_presc_ = 0;
  u64 pwm_synced_ = 0;
  bool pwm_output_ = false;
  LineHook pwm_hook_;

  // KB windows / HSI / HSO
  u8 window_select_ = 0, window_control_ = 0, program_pulse_width_ = 0, hsi_mode_ = 0;
  std::array<bool, 4> hsi_input_{};
  std::array<u8, 4> hsi_transition_count_{};
  struct HsiEntry { u16 time = 0; u8 events = 0; };
  std::array<HsiEntry, 8> hsi_fifo_{};
  u8 hsi_count_ = 0;
  u16 hsi_read_latch_ = 0;
  bool hsi_read_latch_valid_ = false;
  u8 hsi_status_ = 0;
  u16 hso_time_ = 0;
  u8 hso_command_ = 0;
  struct HsoEntry { u16 time = 0; u8 command = 0; bool active = false; };
  std::array<HsoEntry, 8> hso_cam_{};
  HsoEntry hso_holding_{};
  bool hso_holding_valid_ = false;
  bool processing_hso_ = false;
  ByteHook hso_hook_;

  // serial
  u8 serial_rx_ = 0, serial_tx_ = 0, serial_pending_tx_ = 0, serial_control_ = 0, serial_status_ = 0;
  u16 baud_rate_ = 0;
  bool baud_high_byte_ = false;
  bool serial_rx_full_ = false, serial_tx_active_ = false, serial_tx_pending_ = false, serial_tx_line_ = true;
  u8 serial_tx_bit_ = 0, serial_stop_bit_ = 0;
  u32 serial_external_count_ = 0;
  LineHook serial_tx_line_hook_;
  ByteHook serial_tx_byte_hook_;

  // watchdog
  u8 watchdog_ = 0, watchdog_key_ = 0;
  bool watchdog_enabled_ = false;
  u64 watchdog_started_ = 0;
};

}  // namespace mcs96

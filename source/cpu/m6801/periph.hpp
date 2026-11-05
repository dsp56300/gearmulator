// On-chip peripheral block of the MC6801 / HD6301: the four ports, the
// 16-bit timer (free-running counter, output compare, input capture), the
// SCI, the RAM control register and the interrupt logic (NMI, IRQ1, IRQ2).
//
// Nothing is polled per instruction. The free-running counter is an origin
// in time; the timer flags are derived from it when a register is accessed,
// and an event is scheduled only for a transition something can observe (an
// enabled interrupt, the P21 output). The SCI's internal bit clock is the
// counter's, so writing the counter moves it, as on the chip. Register
// accesses see the cycle of their own bus cycle (Cpu::rd8), which makes the
// flag-clearing sequences and the counter reads cycle-exact.
//
// The ports of the two parts built here: port 1 is I/O (the low address
// bus in HD6301 mode 1), port 2 carries the timer and SCI pins and the
// latched mode, ports 3 and 4 are the external bus. Port 3's handshake
// (IS3 / OS3) exists only in the single-chip mode and is not modelled.
#pragma once
#include <algorithm>
#include <array>
#include <deque>
#include <functional>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/m6801/cpu.hpp"

namespace m6801 {

class Peripherals final : public IntcBlock {
 public:
  // Output latch and direction of a port whenever either changes; bits with
  // a clear direction are not driven by the chip.
  using PortWriteHook = std::function<void(u8 data, u8 ddr)>;
  // Pin levels, read when the program reads the port.
  using PortReadHook = std::function<u8()>;
  using ByteHook = std::function<void(u8)>;
  using RamEnableHook = std::function<void(bool enabled)>;

  Peripherals(emu::Scheduler& sched, Cpu& cpu, emu::IoMux& io, ChipModel model, int mode);
  ~Peripherals() override;

  // The register-field bytes the chip decodes itself in `mode` (the rest
  // fall through to the IoMux fallback, the external bus).
  void set_mode(int mode);
  int mode() const { return mode_; }
  ChipModel model() const { return model_; }
  // Reset image of every module (the machine calls it on reset).
  void reset();

  // --- IntcBlock -----------------------------------------------------------------
  bool nmi_pending() const override { return nmi_ff_; }
  bool maskable_pending() const override { return irq1_ff_ || irq1_line_ || irq2_ff_ || irq2_level_; }
  bool sleep_release() const override { return nmi_ff_ || maskable_pending(); }
  u16 acknowledge() override;
  void mask_set() override { irq1_ff_ = irq2_ff_ = false; }

  // --- interrupt pins -------------------------------------------------------------
  // NMI: the assertion (falling edge) is latched until serviced.
  void set_nmi(bool asserted);
  // IRQ1: level sensitive.
  void set_irq1(bool asserted);

  // --- ports (1-4) ----------------------------------------------------------------
  // Levels the board drives on the pins of `port` (default all ones). A
  // change on P20 is an input-capture edge, so P20 must be driven through
  // this rather than a read hook if the program uses the input capture.
  void set_port_input(unsigned port, u8 pins);
  void set_port_read_hook(unsigned port, PortReadHook h) { if (port - 1 < 4) port_[port - 1].read_hook = std::move(h); }
  void set_port_write_hook(unsigned port, PortWriteHook h);
  // Output latch as driven on the pins (timer and SCI outputs included on
  // port 2), and the effective directions.
  u8 port_output(unsigned port) const;
  u8 port_ddr(unsigned port) const;

  // --- timer ------------------------------------------------------------------------
  u16 counter() const { return frc_at(now()); }
  u16 output_compare() const { return ocr_; }
  u16 input_capture() const { return icr_; }
  u8 timer_status();
  bool output_level() const { return olr_; }

  // --- SCI --------------------------------------------------------------------------
  // Bytes arriving on P23 from now on, back to back at the programmed rate;
  // dropped while the receiver is disabled.
  void receive(const u8* data, size_t len);
  void receive(u8 byte) { receive(&byte, 1); }
  // Called with each byte at the end of its stop bit.
  void set_tx_hook(ByteHook h) { tx_hook_ = std::move(h); }
  // The external SCI clock on P22 (CC1:CC0 = 11): its period in E cycles,
  // 16.16 fixed point; the bit rate is one eighth of it. 0: no clock.
  void set_sci_external_clock(u32 period_q16);
  size_t rx_queued() const { return rx_queue_.size(); }
  u8 sci_status() const { return trcsr_; }

  // --- RAM control ------------------------------------------------------------------
  // RAME was written: the machine moves $0080-$00FF between the internal
  // RAM and the external bus.
  void set_ram_enable_hook(RamEnableHook h) { ram_hook_ = std::move(h); }
  bool ram_enabled() const { return (ramc_ & kRame) != 0; }
  u8 ram_control() const { return u8(ramc_ | 0x3F); }
  // Standby power failed: STBY PWR clears (it is set only by a write).
  void standby_power_lost() { ramc_ &= u8(~kStbyPwr); }

 private:
  static constexpr u8 kOlvl = 0x01, kIedg = 0x02, kEtoi = 0x04, kEoci = 0x08, kEici = 0x10, kTof = 0x20, kOcf = 0x40,
                      kIcf = 0x80;
  static constexpr u8 kWu = 0x01, kTe = 0x02, kTie = 0x04, kRe = 0x08, kRie = 0x10, kTdre = 0x20, kOrfe = 0x40,
                      kRdrf = 0x80;
  static constexpr u8 kRame = 0x40, kStbyPwr = 0x80;

  struct Regs final : emu::Device {
    explicit Regs(Peripherals& p) : p(p) {}
    u8 read8(u32 a) override { return p.reg_read(u8(a)); }
    void write8(u32 a, u8 v) override { p.reg_write(u8(a), v); }
    Peripherals& p;
  };
  struct Port {
    u8 ddr = 0, data = 0, pins = 0xFF;
    u8 out = 0xFF, out_ddr = 0;  // last state handed to the write hook
    PortReadHook read_hook;
    PortWriteHook write_hook;
  };

  u64 now() const { return cpu_.now(); }
  void cancel(emu::Scheduler::EventId& e) { if (e) { sched_.cancel(e); e = 0; } }
  u8 reg_read(u8 a);
  void reg_write(u8 a, u8 v);

  // interrupts
  void update_irq2();
  bool irq2_line() const;

  // ports
  u8 pins_in(unsigned i) const { return port_[i].read_hook ? port_[i].read_hook() : port_[i].pins; }
  u8 port_read(unsigned i) const;
  void port2_effective(u8& data, u8& ddr) const;
  void port_changed(unsigned i);
  void p20_check();

  // timer
  u16 frc_at(u64 t) const { return u16(t - frc_origin_); }
  // First cycle at or after `from` during which the counter holds `value`.
  u64 cycle_with(u64 from, u16 value) const { return from + u16(value - frc_at(from)); }
  u64 next_ocf(u64 from) const;
  // The overflow is the counter counting into the value, not being loaded with it.
  u64 next_tof(u64 from) const { return cycle_with(std::max(from, frc_loaded_at_ + 1), tof_value_); }
  void timer_sync(u64 t);
  void timer_reschedule();
  void set_olr(bool level);
  void counter_written(u64 t, u16 value);
  static void on_timer(void* self, u64 when, u64 now);

  // SCI
  bool sci_clock_running() const;
  u64 sci_halfbits(unsigned n) const;  // states of n half bit times
  u64 sci_next_edge(u64 t) const;  // first bit-clock edge after t
  bool sci_irq() const;
  void sci_sync(u64 t);
  void sci_tx_schedule(u64 t);
  void sci_tx_event(u64 when);
  void sci_tx_load(u64 at);
  void sci_rx_start(u64 t);
  void sci_rx_event(u64 when);
  void sci_wu_schedule(u64 t);
  static void on_tx(void* self, u64 when, u64 now);
  static void on_rx(void* self, u64 when, u64 now);
  static void on_wu(void* self, u64 when, u64 now);

  emu::Scheduler& sched_;
  Cpu& cpu_;
  emu::IoMux& io_;
  ChipModel model_;
  Family family_;
  int mode_ = 0;
  Regs regs_{*this};

  // interrupts
  bool nmi_line_ = false, nmi_ff_ = false;
  bool irq1_line_ = false, irq1_ff_ = false;
  bool irq2_level_ = false, irq2_ff_ = false;

  // ports
  std::array<Port, 4> port_{};
  bool p20_ = true;

  // timer
  u64 frc_origin_ = 0;   // the counter holds (t - frc_origin_) during cycle t
  u64 frc_loaded_at_ = 0;  // the cycle it last held a value it did not count to (reset, a write)
  u64 synced_ = 0;       // compare / overflow matches before this cycle are applied
  u64 ocf_inhibit_lo_ = 1, ocf_inhibit_hi_ = 0;  // compare inhibited for these cycles
  u16 ocr_ = 0xFFFF, icr_ = 0, tof_value_ = 0xFFFF;
  u8 tcsr_ = 0, tcsr_armed_ = 0;  // flags seen by a TCSR read, cleared by the matching access
  u8 frc_lsb_ = 0;                // low byte buffered by a read of the high byte
  u8 frc_msb_ = 0;                // HD6301: the high byte written, waiting for the low byte
  bool olr_ = false;              // output level register (P21)
  emu::Scheduler::EventId timer_event_ = 0;

  // SCI
  u8 rmcr_ = 0, trcsr_ = kTdre, rdr_ = 0, tdr_ = 0, sci_armed_ = 0;
  u32 ext_clock_q16_ = 0;
  u64 ext_origin_ = 0;
  bool tx_busy_ = false;     // a frame is on the line until tx_ready_at_
  u64 tx_ready_at_ = 0;      // end of the preamble / of the last frame
  u8 tx_shift_ = 0;
  emu::Scheduler::EventId tx_event_ = 0;
  u64 tx_event_at_ = 0;
  ByteHook tx_hook_;
  std::deque<u8> rx_queue_;
  bool rx_busy_ = false;
  u8 rx_shift_ = 0;
  u64 rx_start_ = 0, rx_line_free_ = 0;
  u64 rx_idle_since_ = 0;    // the receive line has been a mark since then
  emu::Scheduler::EventId rx_event_ = 0, wu_event_ = 0;
  u64 rx_event_at_ = 0, wu_event_at_ = 0;

  // RAM control
  u8 ramc_ = 0;
  RamEnableHook ram_hook_;
};

}  // namespace m6801

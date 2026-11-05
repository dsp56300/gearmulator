// On-chip peripheral block of the MC68HC11A0 (the HC11A8 without ROM and
// EEPROM): ports A, D and E, the main timer (free-running counter, three
// input captures, five output compares and their pin actions, overflow), the
// real-time interrupt, the pulse accumulator, the SCI, the SPI (master), the
// A/D converter and the interrupt priority logic with HPRIO's promotion.
//
// The 64-byte register block sits at $1000, INIT's reset value; the remap is
// not built. In the expanded mode PORTB, PORTC, DDRC, PORTCL and PIOC are not
// decoded by the chip (a port replacement unit answers there), so those bytes
// fall through to the board.
//
// Same approach as the MC6801 block: nothing ticks. The counter is an origin
// in time, the flags are derived from it when a register is read, and an
// event is scheduled only for a transition something can observe (an enabled
// interrupt, an output-compare pin action, a serial frame). Every source is
// a level on the HC11 (flag and enable), so there are no flip-flops; the
// optional IRQ edge mode (OPTION IRQE) keeps one latch.
//
// Not modelled: the COP watchdog and the clock monitor (the A0's CONFIG has
// NOCOP set), the STOP recovery delay, the SCI's receiver wake-up and break,
// the SPI slave mode, the parallel I/O handshake (external in this mode).
#pragma once
#include <array>
#include <deque>
#include <functional>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/m6801/cpu.hpp"

namespace m6801 {

namespace hc11 {
// Register block (offsets from $1000).
constexpr u16 kRegBase = 0x1000, kRegSize = 0x40;
constexpr u16 kPorta = 0x00, kPioc = 0x02, kPortc = 0x03, kPortb = 0x04, kPortcl = 0x05, kDdrc = 0x07,
              kPortd = 0x08, kDdrd = 0x09, kPorte = 0x0A, kCforc = 0x0B, kOc1m = 0x0C, kOc1d = 0x0D, kTcnt = 0x0E,
              kTic1 = 0x10, kToc1 = 0x16, kTctl1 = 0x20, kTctl2 = 0x21, kTmsk1 = 0x22, kTflg1 = 0x23, kTmsk2 = 0x24,
              kTflg2 = 0x25, kPactl = 0x26, kPacnt = 0x27, kSpcr = 0x28, kSpsr = 0x29, kSpdr = 0x2A, kBaud = 0x2B,
              kSccr1 = 0x2C, kSccr2 = 0x2D, kScsr = 0x2E, kScdr = 0x2F, kAdctl = 0x30, kAdr1 = 0x31, kOption = 0x39,
              kCoprst = 0x3A, kPprog = 0x3B, kHprio = 0x3C, kInit = 0x3D, kTest1 = 0x3E, kConfig = 0x3F;
constexpr u16 kRamBase = 0x0000, kRamSize = 0x100;
}  // namespace hc11

class Hc11Peripherals final : public IntcBlock {
 public:
  enum Port : unsigned { kPortA = 0, kPortD = 1, kPortE = 2 };
  // Output latch and direction of a port whenever either changes; bits with
  // a clear direction are not driven by the chip.
  using PortWriteHook = std::function<void(u8 data, u8 ddr)>;
  // Pin levels, read when the program reads the port.
  using PortReadHook = std::function<u8()>;
  using ByteHook = std::function<void(u8)>;
  // SPI master transfer: the byte shifted out, returns the byte shifted in.
  using SpiHook = std::function<u8(u8)>;

  Hc11Peripherals(emu::Scheduler& sched, Cpu& cpu, emu::IoMux& io);
  ~Hc11Peripherals() override;

  // Reset image of every module (the machine calls it on reset).
  void reset();

  // --- IntcBlock -----------------------------------------------------------------
  bool nmi_pending() const override { return xirq_ && !(cpu_.ccr() & kX); }
  bool maskable_pending() const override { return pending_sources() != 0; }
  bool sleep_release() const override { return xirq_ || (!(cpu_.ccr() & kI) && maskable_pending()); }
  u16 acknowledge() override;
  void mask_set() override {}

  // --- interrupt pins -------------------------------------------------------------
  // XIRQ: level sensitive, masked by X.
  void set_xirq(bool asserted);
  // IRQ: level sensitive, or the falling edge latched (OPTION IRQE).
  void set_irq(bool asserted);

  // --- ports A, D, E ---------------------------------------------------------------
  // Levels the board drives on the pins (default all ones). An edge on
  // PA0-PA2 (input captures) or PA7 (pulse accumulator) must come through
  // here, not through a read hook, for those functions to see it.
  void set_port_input(Port p, u8 pins);
  void set_port_read_hook(Port p, PortReadHook h) { port_[p].read_hook = std::move(h); }
  void set_port_write_hook(Port p, PortWriteHook h);
  // Output latch as driven on the pins (compare actions and TxD included) and the effective directions.
  u8 port_output(Port p) const;
  u8 port_ddr(Port p) const;

  // --- A/D --------------------------------------------------------------------------
  // The level on channel `ch` (0-7 the PE pins), full scale $FF.
  void set_analog_input(unsigned ch, u8 value) { if (ch < 8) analog_[ch] = value; }

  // --- SCI --------------------------------------------------------------------------
  // Bytes arriving on RxD from now on, back to back at the programmed rate;
  // dropped while the receiver is disabled.
  void receive(const u8* data, size_t len);
  void receive(u8 byte) { receive(&byte, 1); }
  // Called with each byte at the end of its stop bit.
  void set_tx_hook(ByteHook h) { tx_hook_ = std::move(h); }
  size_t rx_queued() const { return rx_queue_.size(); }
  u8 sci_status() const { return scsr_; }
  // E cycles per bit at the BAUD register's rate.
  u32 sci_bit_cycles() const;

  // --- SPI --------------------------------------------------------------------------
  void set_spi_hook(SpiHook h) { spi_hook_ = std::move(h); }

  // --- timer ------------------------------------------------------------------------
  u16 counter() const { return count_at(now()); }
  u8 timer_flags1() { timer_sync(now()); return tflg1_; }
  u8 timer_flags2() { timer_sync(now()); return tflg2_; }

 private:
  // Interrupt sources, highest fixed priority first.
  enum Source : unsigned { kSrcIrq, kSrcRti, kSrcIc1, kSrcIc2, kSrcIc3, kSrcOc1, kSrcOc2, kSrcOc3, kSrcOc4, kSrcOc5,
                           kSrcTof, kSrcPaov, kSrcPai, kSrcSpi, kSrcSci, kSources };

  static constexpr u8 kTof = 0x80, kRtif = 0x40, kPaovf = 0x20, kPaif = 0x10;  // TFLG2 / TMSK2
  static constexpr u8 kTdre = 0x80, kTc = 0x40, kRdrf = 0x20, kIdle = 0x10, kOr = 0x08, kNf = 0x04, kFe = 0x02;
  static constexpr u8 kTie = 0x80, kTcie = 0x40, kRie = 0x20, kIlie = 0x10, kTe = 0x08, kRe = 0x04;
  static constexpr u8 kSpif = 0x80, kWcol = 0x40;
  static constexpr u8 kSpie = 0x80, kSpe = 0x40, kMstr = 0x10;
  static constexpr u8 kDdra7 = 0x80, kPaen = 0x40, kPamod = 0x20, kPedge = 0x10;
  static constexpr u8 kIrqe = 0x20;

  struct Regs final : emu::Device {
    explicit Regs(Hc11Peripherals& p) : p(p) {}
    u8 read8(u32 a) override { return p.reg_read(u8(a - hc11::kRegBase)); }
    void write8(u32 a, u8 v) override { p.reg_write(u8(a - hc11::kRegBase), v); }
    Hc11Peripherals& p;
  };
  struct PortState {
    u8 pins = 0xFF;
    u8 out = 0xFF, out_ddr = 0;  // last state handed to the write hook
    PortReadHook read_hook;
    PortWriteHook write_hook;
  };

  u64 now() const { return cpu_.now(); }
  void cancel(emu::Scheduler::EventId& e) { if (e) { sched_.cancel(e); e = 0; } }
  // The registers whose writes count only in the first 64 E cycles after reset.
  bool protected_window() const { return now() < reset_at_ + 64; }
  u8 reg_read(u8 a);
  void reg_write(u8 a, u8 v);

  // interrupts
  u32 pending_sources() const;
  void update_irq() { cpu_.reeval_irq(); }

  // ports
  u8 pins_in(Port p) const { return port_[p].read_hook ? port_[p].read_hook() : port_[p].pins; }
  u8 porta_read() const;
  u8 portd_effective(u8& ddr) const;
  void port_changed(Port p);
  // Port A bits an output-compare function drives (a CPU write leaves them).
  u8 porta_oc_pins() const;

  // timer
  u16 count_at(u64 t) const { return u16((t - origin_) / prescale_); }
  // First counter tick at or after `from` that brings the counter to `value`.
  u64 tick_with(u64 from, u16 value) const;
  u64 oc_match(unsigned i, u64 from) const;
  u64 rti_period() const { return u64(8192) << (pactl_ & 3); }
  u64 next_rti(u64 from) const;
  bool oc_pin_action(unsigned i) const;
  void oc_apply(unsigned i);
  void timer_sync(u64 t);
  void timer_reschedule();
  static void on_timer(void* self, u64 when, u64 now);
  void capture(unsigned ic, u64 t);

  // pulse accumulator
  bool pa_gate_open() const;
  void pa_sync(u64 t);
  void pa_count(unsigned n);
  u64 pa_overflow_at() const;
  void pa_input_changed(bool old_level, bool level);

  // SCI
  u32 frame_cycles() const { return sci_bit_cycles() * ((sccr1_ & 0x10) ? 11 : 10); }
  u64 sci_next_edge(u64 t) const;
  bool sci_irq() const;
  void sci_sync(u64 t);
  void sci_tx_schedule(u64 t);
  void sci_tx_event(u64 when);
  void sci_rx_start(u64 t);
  void sci_rx_event(u64 when);
  void sci_idle_schedule();
  static void on_tx(void* self, u64 when, u64 now);
  static void on_rx(void* self, u64 when, u64 now);
  static void on_idle(void* self, u64 when, u64 now);

  // SPI
  void spi_sync(u64 t);
  static void on_spi(void* self, u64 when, u64 now);

  // A/D
  void adc_sync(u64 t);
  unsigned adc_channel(unsigned k) const;

  emu::Scheduler& sched_;
  Cpu& cpu_;
  emu::IoMux& io_;
  Regs regs_{*this};
  u64 reset_at_ = 0;

  // interrupts
  bool xirq_ = false, irq_line_ = false, irq_edge_ = false;
  u8 option_ = 0x10, hprio_ = 0x25, init_ = 0x01;

  // ports
  std::array<PortState, 3> port_{};
  u8 pa_latch_ = 0, pd_latch_ = 0, ddrd_ = 0;

  // timer
  u64 origin_ = 0;      // tick k (the counter holding k) starts at origin_ + k * prescale_
  u32 prescale_ = 1;
  u64 synced_ = 0;      // matches before this cycle are applied
  std::array<u16, 5> toc_{};
  std::array<u16, 3> tic_{};
  std::array<u64, 5> oc_inhibit_{};  // a compare in this cycle (or the next) is inhibited by a write
  u8 tflg1_ = 0, tmsk1_ = 0, tflg2_ = 0, tmsk2_ = 0, tctl1_ = 0, tctl2_ = 0, oc1m_ = 0, oc1d_ = 0;
  u8 tcnt_lsb_ = 0;
  u64 tcnt_lsb_at_ = ~u64(0);  // the cycle in which a read of the low byte returns the buffer
  emu::Scheduler::EventId timer_event_ = 0;

  // pulse accumulator and RTI
  u8 pactl_ = 0, pacnt_ = 0;
  bool pai_ = true;      // PA7 level
  u64 pa_synced_ = 0;    // gated mode: E/64 ticks before this cycle are counted

  // SCI
  u8 baud_ = 0, sccr1_ = 0, sccr2_ = 0, scsr_ = kTdre | kTc, rdr_ = 0, tdr_ = 0, sci_armed_ = 0;
  bool tx_busy_ = false;
  u64 tx_ready_at_ = 0;  // end of the preamble / of the last frame
  u8 tx_shift_ = 0;
  emu::Scheduler::EventId tx_event_ = 0;
  u64 tx_event_at_ = 0;
  ByteHook tx_hook_;
  std::deque<u8> rx_queue_;
  bool rx_busy_ = false, idle_armed_ = false;
  u8 rx_shift_ = 0;
  u64 rx_start_ = 0, rx_line_free_ = 0;
  emu::Scheduler::EventId rx_event_ = 0, idle_event_ = 0;
  u64 rx_event_at_ = 0, idle_event_at_ = 0;

  // SPI
  u8 spcr_ = 0x04, spsr_ = 0, spdr_ = 0, spi_out_ = 0, spi_armed_ = 0;
  bool spi_busy_ = false;
  SpiHook spi_hook_;
  emu::Scheduler::EventId spi_event_ = 0;
  u64 spi_event_at_ = 0;

  // A/D
  u8 adctl_ = 0;
  bool adc_running_ = false;
  u64 adc_start_ = 0;
  unsigned adc_done_ = 0;  // conversions of the current sequence stored
  std::array<u8, 4> adr_{};
  std::array<u8, 8> analog_{};
};

}  // namespace m6801

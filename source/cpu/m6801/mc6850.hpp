// Motorola MC6850 ACIA on the shared event scheduler.
//
// Two register addresses (RS = address bit 0): status / control and receive
// / transmit data. The transmit and receive clocks are one input here
// (boards tie TxC and RxC together), given as its period in machine states;
// the control register divides it by 1, 16 or 64 for the bit rate. Frames
// are timed from the clock: a byte written to the transmit register goes to
// the shifter at the next bit edge once the shifter is free and reaches the
// hook at the end of its last stop bit; a byte handed to receive() lands in
// the receive register in the middle of its last stop bit.
//
// The chip powers up in no defined state: nothing runs until the program
// master-resets it (CR1:CR0 = 11) and then writes a working divide ratio.
//
// Not modelled: parity generation and checking (the data byte is passed as
// is, the parity error flag never sets), framing errors, break, the RTS
// output and the CTS / DCD inputs (CTS and DCD held low, i.e. active).
#pragma once
#include <deque>
#include <functional>

#include "common/device.hpp"
#include "common/sched.hpp"

namespace m6801 {

class Mc6850 final : public emu::Device {
 public:
  using ByteHook = std::function<void(emu::u8)>;
  using IrqHook = std::function<void(bool asserted)>;

  static constexpr emu::u8 kRdrf = 0x01, kTdre = 0x02, kDcd = 0x04, kCts = 0x08, kFe = 0x10, kOvrn = 0x20,
                           kPe = 0x40, kIrq = 0x80;

  Mc6850(emu::Scheduler& sched, const emu::Clock& clock);
  ~Mc6850() override;

  // The period of the TxC / RxC clock in machine states, 16.16 fixed point.
  void set_clock_period(emu::u32 states_q16) { clock_q16_ = states_q16; }

  // Power-on: the master-reset state, both directions idle.
  void power_on();

  // RS (address bit 0): 0 status / control, 1 data.
  emu::u8 read8(emu::u32 addr) override;
  void write8(emu::u32 addr, emu::u8 value) override;
  // Status without the side effects of a read.
  emu::u8 status() const { return status_; }

  // Bytes arriving on RxD from now on, back to back at the programmed rate;
  // dropped while the chip is held in master reset.
  void receive(const emu::u8* data, size_t len);
  void receive(emu::u8 byte) { receive(&byte, 1); }
  size_t rx_queued() const { return rx_queue_.size(); }

  // Called with each byte at the end of its last stop bit.
  void set_tx_hook(ByteHook h) { tx_hook_ = std::move(h); }
  // The IRQ output whenever it changes.
  void set_irq_hook(IrqHook h) { irq_hook_ = std::move(h); }
  bool irq() const { return (status_ & kIrq) != 0; }

 private:
  bool in_reset() const { return (control_ & 3) == 3; }
  // States per bit, 16.16.
  emu::u64 bit_q16() const;
  // Start bit, seven or eight data bits, parity, one or two stop bits (CR4-CR2).
  unsigned frame_bits() const;
  emu::u64 bits(unsigned n) const { return (bit_q16() * n + 0xFFFF) >> 16; }
  emu::u64 next_edge(emu::u64 t) const;
  void update_irq();
  void sync(emu::u64 t);
  void tx_schedule(emu::u64 t);
  void tx_event(emu::u64 when);
  void rx_start(emu::u64 t);
  void rx_event(emu::u64 when);
  static void on_tx(void* self, emu::u64 when, emu::u64 now);
  static void on_rx(void* self, emu::u64 when, emu::u64 now);
  void cancel(emu::Scheduler::EventId& e) { if (e) { sched_.cancel(e); e = 0; } }

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  emu::u32 clock_q16_ = 1u << 16;
  emu::u64 origin_ = 0;  // the bit-rate divider restarted here (end of the master reset)

  emu::u8 control_ = 3, status_ = 0, rdr_ = 0, tdr_ = 0;

  bool tx_busy_ = false;
  emu::u64 tx_ready_at_ = 0;
  emu::u8 tx_shift_ = 0;
  emu::Scheduler::EventId tx_event_ = 0;
  emu::u64 tx_event_at_ = 0;
  ByteHook tx_hook_;

  std::deque<emu::u8> rx_queue_;
  bool rx_busy_ = false;
  emu::u8 rx_shift_ = 0;
  emu::u64 rx_start_ = 0, rx_line_free_ = 0;
  emu::Scheduler::EventId rx_event_ = 0;
  emu::u64 rx_event_at_ = 0;

  IrqHook irq_hook_;
};

}  // namespace m6801

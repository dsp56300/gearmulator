// Serial communication interface (H8/510 hardware manual section 13).
//
// Registers (channel 1 at H'FEC8, channel 2 at H'FED0):
//   +0 SMR  C/A CHR PE O/E STOP 1 CKS1 CKS0        reset H'04
//   +1 BRR  bit rate constant N                    reset H'FF
//   +2 SCR  TIE RIE TE RE 1 1 CKE1 CKE0            reset H'0C
//   +3 TDR  transmit data                          reset H'FF
//   +4 SSR  TDRE RDRF ORER FER PER 1 1 1           reset H'87 (flags: read 1 then write 0)
//   +5 RDR  receive data (read-only)               reset H'00
//
// Bit time in states: asynchronous 32 * 4^n * (N + 1), synchronous
// 4 * 4^n * (N + 1) (tables 13-3 / 13-4 with phi = OSC / 2).  A frame is
// start + 7/8 data + parity + 1/2 stop bits (8 bits in synchronous mode).
//
// Transmit: TDR and the transmit shift register double-buffer.  Clearing TDRE
// with TE set loads the TSR as soon as it is free (TDRE goes back to 1, TXI);
// the byte reaches the host sink when its frame ends.  Setting TE first sends
// one frame of all ones.  Receive: the host pushes bytes; the wire delivers
// them one frame apart; a byte completing while RDRF is still set is lost
// with ORER, framing/parity errors set FER/PER and still update RDR (table
// 13-10).  TXI = TIE & TDRE, RXI = RIE & RDRF, ERI = RIE & (ORER|FER|PER).
#pragma once
#include <deque>
#include <functional>

#include "cpu/h8500/bus.hpp"
#include "common/iomux.hpp"
#include "cpu/h8500/intc.hpp"
#include "common/sched.hpp"

namespace h8500 {

class Sci final : public Device {
 public:
  // SMR bits
  static constexpr u8 kCa = 0x80, kChr = 0x40, kPe = 0x20, kOe = 0x10, kStop = 0x08;
  // SCR bits
  static constexpr u8 kTie = 0x80, kRie = 0x40, kTe = 0x20, kRe = 0x10, kCke1 = 0x02, kCke0 = 0x01;
  // SSR bits
  static constexpr u8 kTdre = 0x80, kRdrf = 0x40, kOrer = 0x20, kFer = 0x10, kPer = 0x08;

  struct Sources { IrqSrc eri, rxi, txi; };
  // Receives a transmitted byte and the exact state at which its frame ended
  // (the callback itself runs at the following instruction boundary).
  using TxSink = std::function<void(u8 byte, u64 at)>;

  Sci(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base, Sources src);
  ~Sci() override;

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  // --- host side -----------------------------------------------------------
  // Called when a transmitted frame completes (byte already masked to 7 bits
  // in 7-bit mode).
  void set_tx_sink(TxSink sink) { tx_sink_ = std::move(sink); }
  // A byte arriving on RXD.  Frames queue up and complete one frame time
  // apart, as the wire would deliver them.  `framing_error` marks a 0 stop
  // bit (a line break is H'00 with a framing error), `parity_error` a bad
  // parity bit.
  void receive_byte(u8 byte, bool framing_error = false, bool parity_error = false);
  // Period of an external serial clock on SCK (CKE1 = 1), in states per
  // bit; 0 (default) falls back to the internal formula.
  void set_external_bit_time(u64 states) { ext_bit_states_ = states; }
  // Called when a received frame lands in RDR (the H8/570 ISP watches RXD).
  void set_rx_hook(std::function<void()> hook) { rx_hook_ = std::move(hook); }
  size_t rx_pending() const { return rx_queue_.size(); }

  // --- DTC cooperation -----------------------------------------------------
  void dtc_wrote_tdr(u8 value);  // clears TDRE
  u8 dtc_read_rdr();             // clears RDRF

  // --- introspection ---------------------------------------------------------
  u8 ssr(u64 now) { sync(now); return u8(ssr_ | 0x07); }
  u64 bit_states() const;
  u64 frame_states() const;

 private:
  struct RxFrame { u8 byte; bool fer, per; };

  static void on_tx_event(void* self, u64 when, u64 now);
  static void on_rx_event(void* self, u64 when, u64 now);
  void sync(u64 now);
  void start_tx_frame(u64 now);
  void complete_rx_frame(u64 now);
  void schedule_tx(u64 when);
  void schedule_rx(u64 when);
  void update_requests();
  bool tsr_busy(u64 now) const { return tx_end_ != 0 && now < tx_end_; }

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  u32 base_;
  Sources src_;
  TxSink tx_sink_;
  std::function<void()> rx_hook_;
  emu::Scheduler::EventId tx_event_ = 0, rx_event_ = 0;

  u8 smr_ = 0x04, brr_ = 0xFF, scr_ = 0x0C, tdr_ = 0xFF, ssr_ = 0x87, rdr_ = 0;
  u8 flags_read_ = 0;
  u64 ext_bit_states_ = 0;

  // Transmit shift register: byte in flight and the time its frame ends
  // (0 = idle).  `tsr_valid_` is false for the all-ones preamble after TE.
  u8 tsr_ = 0xFF;
  bool tsr_valid_ = false;
  u64 tx_end_ = 0;

  // Receive: frames waiting on the wire and the completion time of the one
  // currently being received (0 = line idle).
  std::deque<RxFrame> rx_queue_;
  u64 rx_end_ = 0;
  u64 now_ = 0;
};

}  // namespace h8500

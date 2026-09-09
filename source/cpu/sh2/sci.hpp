// Serial communication interface (SH7014 hardware manual section 12).
//
// Two identical channels; registers (channel 0 at H'FFFF81A0, channel 1 at
// H'FFFF81B0; 8-bit access, table 8.5; +6..+15 are empty):
//   +0 SMR  C/A CHR PE O/E STOP MP CKS1 CKS0       reset H'00
//   +1 BRR  bit rate constant N                    reset H'FF
//   +2 SCR  TIE RIE TE RE MPIE TEIE CKE1 CKE0      reset H'00
//   +3 TDR  transmit data                          reset H'FF
//   +4 SSR  TDRE RDRF ORER FER PER TEND MPB MPBT   reset H'84
//           (TDRE..PER: read 1 then write 0 clears; TEND, MPB read-only; MPBT R/W)
//   +5 RDR  receive data (read-only)               reset H'00
//
// Bit time in states of phi (12.2.8, formulas inverted): asynchronous
// 64 * 2^(2n-1) * (N + 1) = 32 * 4^n * (N + 1); clocked synchronous
// 8 * 2^(2n-1) * (N + 1) = 4 * 4^n * (N + 1) (checked against tables 12.3 /
// 12.4).  A frame is start + 7/8 data + parity-or-MPB + 1/2 stop bits in
// asynchronous mode (table 12.10), 8 bits in clocked synchronous mode.
//
// Wire model (same as the H8/500 SCI): frame-granular.  Transmit: TDR and
// the TSR double-buffer.  Clearing TDRE (read 1, write 0; or a DMAC write to
// TDR) with TE set clears TEND and loads the TSR as soon as it is free; the
// load sets TDRE back to 1 (TXI) and the byte reaches the host sink when its
// frame ends.  At a frame end with TDRE still 1 the transmitter goes idle and
// sets TEND (TEI).  Receive: the host pushes whole frames; the wire delivers
// them one frame time apart; on completion, with RE set, table 12.13 applies:
// a frame completing while RDRF is still 1 is lost with ORER (its FER/PER are
// still recorded); a framing or parity error moves the data to RDR but does
// not set RDRF; otherwise RDR is loaded and RDRF set (RXI).  While any of
// ORER/FER/PER is set the receiver keeps checking frames but neither loads
// RDR nor sets RDRF (12.3.2 note, 12.5.3).  Multiprocessor format (12.3.3):
// the frame's MPB is latched in SSR.MPB; with MPIE set, frames with MPB = 0
// are skipped entirely (no RDR transfer, no flags) until an ID frame
// (MPB = 1) arrives, which clears MPIE and is received normally.  In clocked
// synchronous mode a set receive-error flag also holds off transmission
// (12.5.5).  TXI = TIE & TDRE, RXI = RIE & RDRF, ERI = RIE & (ORER|FER|PER),
// TEI = TEIE & TEND.
//
// Approximations (all frame-granular consequences of the model):
//   * The TDRE check the hardware makes "when it outputs the stop bit / MSB"
//     (12.3.2 step 3) is made at the frame end: TDR -> TSR, TXI and TEND move
//     by less than one bit time.
//   * Setting TE puts the line in the marking state (12.3.2 initialisation
//     step 4) and nothing is sent until TDRE is cleared; clearing TE
//     initialises the transmitter regardless of its status (12.5.4), so a
//     frame in flight is abandoned and never reaches the sink.
//   * The 16x receive sampling clock, receive margin (12.5.6), break
//     detection by reading RxD (12.5.3), the SCK waveform and the external
//     clock constraints of 12.5.7-12.5.9 are not modelled: the host supplies
//     framing/parity error attributes with each frame and, for CKE1 = 1, the
//     external bit period.
#pragma once
#include <deque>
#include <functional>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/intc.hpp"

namespace sh2 {

class Sci final : public Device {
 public:
  static constexpr u32 kBase0 = 0xFFFF81A0u, kBase1 = 0xFFFF81B0u;
  // SMR bits
  static constexpr u8 kCa = 0x80, kChr = 0x40, kPe = 0x20, kOe = 0x10, kStop = 0x08, kMp = 0x04;
  // SCR bits
  static constexpr u8 kTie = 0x80, kRie = 0x40, kTe = 0x20, kRe = 0x10, kMpie = 0x08, kTeie = 0x04,
                      kCke1 = 0x02, kCke0 = 0x01;
  // SSR bits
  static constexpr u8 kTdre = 0x80, kRdrf = 0x40, kOrer = 0x20, kFer = 0x10, kPer = 0x08, kTend = 0x04,
                      kMpb = 0x02, kMpbt = 0x01;
  static constexpr u8 kErrors = kOrer | kFer | kPer;

  struct Sources { IrqSrc eri, rxi, txi, tei; };
  // Receives a transmitted byte (masked to 7 bits in 7-bit mode), the
  // multiprocessor bit that went with it (MPBT; false outside the
  // multiprocessor format) and the exact state at which its frame ended (the
  // callback itself runs at the following instruction boundary).
  using TxSink = std::function<void(u8 byte, bool mpb, u64 at)>;

  Sci(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base, Sources src);
  ~Sci() override;

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  // --- host side -----------------------------------------------------------
  void set_tx_sink(TxSink sink) { tx_sink_ = std::move(sink); }
  // A frame arriving on RxD.  Frames queue up and complete one frame time
  // apart, as the wire would deliver them.  `mpb` is the multiprocessor bit
  // (only meaningful in the multiprocessor format), `framing_error` marks a 0
  // stop bit (a line break is H'00 with a framing error), `parity_error` a
  // bad parity bit (ignored unless PE is set in a non-multiprocessor
  // asynchronous format).
  void receive_byte(u8 byte, bool mpb = false, bool framing_error = false, bool parity_error = false);
  // Period of an external serial clock on SCK (CKE1 = 1), in states per bit
  // (asynchronous: the pin clock is 16x this rate, table 12.9); 0 (default)
  // falls back to the internal baud rate generator formula.
  void set_external_bit_time(u64 states) { ext_bit_states_ = states; }
  size_t rx_pending() const { return rx_queue_.size(); }
  // Table 12.9: SCK drives a clock (asynchronous: CKE1:0 = 01; clocked
  // synchronous: CKE1 = 0); otherwise SCK is an input (CKE1 = 1) or unused.
  bool sck_output() const { return !(scr_ & kCke1) && ((smr_ & kCa) || (scr_ & kCke0)); }
  bool external_clock() const { return (scr_ & kCke1) != 0; }

  // --- DMAC cooperation (12.4) -------------------------------------------------
  void dma_wrote_tdr(u8 value);  // clears TDRE (and TEND)
  u8 dma_read_rdr();             // clears RDRF

  // --- introspection ---------------------------------------------------------
  u8 ssr(u64 now) { sync(now); return ssr_; }
  u64 bit_states() const;
  u64 frame_states() const;

 private:
  struct RxFrame { u8 byte; bool mpb, fer, per; };

  static void on_tx_event(void* self, u64 when, u64 now);
  static void on_rx_event(void* self, u64 when, u64 now);
  void sync(u64 now);
  void start_tx_frame(u64 at);
  void try_start_tx(u64 now);
  void complete_rx_frame(u64 at);
  void schedule_tx(u64 when);
  void schedule_rx(u64 when);
  void update_requests();
  bool tsr_busy(u64 now) const { return tx_end_ != 0 && now < tx_end_; }
  bool seven_bit() const { return (smr_ & kChr) && !(smr_ & kCa); }
  bool mp_format() const { return (smr_ & kMp) && !(smr_ & kCa); }
  // 12.5.5: in clocked synchronous mode a receive error flag blocks transmission.
  bool tx_allowed() const { return !((smr_ & kCa) && (ssr_ & kErrors)); }

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  u32 base_;
  Sources src_;
  TxSink tx_sink_;
  emu::Scheduler::EventId tx_event_ = 0, rx_event_ = 0;

  u8 smr_ = 0x00, brr_ = 0xFF, scr_ = 0x00, tdr_ = 0xFF, ssr_ = 0x84, rdr_ = 0;
  u8 flags_read_ = 0;
  u64 ext_bit_states_ = 0;

  // Transmit shift register: byte (and MPB) in flight and the time its frame
  // ends (0 = idle, marking).
  u8 tsr_ = 0xFF;
  bool tsr_mpb_ = false;
  u64 tx_end_ = 0;

  // Receive: frames waiting on the wire and the completion time of the one
  // currently being received (0 = line idle).
  std::deque<RxFrame> rx_queue_;
  u64 rx_end_ = 0;
  u64 now_ = 0;
};

}  // namespace sh2

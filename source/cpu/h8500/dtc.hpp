// Data transfer controller (H8/510 hardware manual section 6).
//
// An interrupt whose DTE bit is set starts the DTC instead of the CPU.  The
// DTC reads the address of a four-word register information table from the
// DTC vector table (H'C0-H'F9 in minimum mode, H'180-H'1F3 in maximum mode,
// page 0), then:
//   DTMR (TA+0)  Sz(15) SI(14) DI(13): word/byte transfer, increment source/dest
//   DTSR (TA+2)  source address (page 0)
//   DTDR (TA+4)  destination address (page 0)
//   DTCR (TA+6)  transfers remaining (0 = 65536)
// One byte or word is moved, the incremented addresses and the decremented
// count are written back, the requesting module's flag is cleared, and when
// the count reaches zero a CPU interrupt with the source's own vector is
// raised.  The CPU is stalled for the transfer: 26 + 2*SI + 2*DI + Ms + Md
// states (table 6-5; Ms/Md = 2 for 16-bit 2-state memory, 3 for a byte and 6
// for a word in an 8-bit 3-state area or the register field).
//
// Requests are queued from the interrupt controller and served at the next
// instruction boundary by Machine::run (transfers never interleave with an
// instruction).
#pragma once
#include <functional>

#include "cpu/h8500/bus.hpp"
#include "cpu/h8500/cpu.hpp"
#include "cpu/h8500/intc.hpp"

namespace h8500 {

class Dtc final : public DtcClient {
 public:
  // Clears the module flag for a served interrupt; `data` is the transferred
  // value (SCI TXI uses it as the byte written to TDR).
  using FlagClear = std::function<void(u16 data)>;

  Dtc(Bus& bus, Cpu& cpu, Intc& intc);

  void reset();
  void attach(IrqSrc src, FlagClear clear) { clear_[size_t(src)] = std::move(clear); }

  bool dtc_request(IrqSrc src, u8 vector) override;
  // Perform every queued transfer.  Call at an instruction boundary.
  void service();
  bool pending() const { return pending_ != 0; }
  u64 transfers() const { return transfers_; }

  // DTC vector table address for a source (minimum-mode byte address), 0 if
  // the source cannot start the DTC.
  static u32 vector_addr(IrqSrc src);

 private:
  void transfer(IrqSrc src, u8 vector);
  unsigned access_states(u32 addr, bool word) const;

  Bus& bus_;
  Cpu& cpu_;
  Intc& intc_;
  u32 pending_ = 0;
  u8 vector_[size_t(IrqSrc::Count)] = {};
  FlagClear clear_[size_t(IrqSrc::Count)];
  u64 transfers_ = 0;
};

}  // namespace h8500

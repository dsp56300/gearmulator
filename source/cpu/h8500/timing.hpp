// Instruction execution timing (H8/500 programming manual 2.6, H8/510 hardware
// manual appendix A.4, tables A-7 / A-8).
//
// Total states for one instruction:
//
//   states = base(insn, mode, condition)                     Table A-7 / 2-8
//          + parity_adjustment(insn, start address odd?)     Table A-8 / 2-9 (16-bit fetch only)
//          + fetch_penalty(fetch bus class, J+K)
//          + operand_penalty(operand bus class, I, size)
//          + wait states
//
// where I is the number of operand bytes read/written in memory and J+K the
// number of instruction bytes fetched (J: EA field, K: OP field; branch rows
// include the refetch after the jump), both taken from the tables.  All base
// values assume 16-bit, two-state accesses; the penalty terms implement the
// correction formulas of 2.6.1 (8-bit-bus devices) and 2.6.4 (H8/510/570).
#pragma once
#include "cpu/h8500/decode.hpp"

namespace h8500 {

struct BaseTiming {
  u8 states = 0;  // execution states (16-bit 2-state accesses assumed)
  u8 I = 0;       // bytes of operand read/written in memory
  u8 JK = 0;      // instruction bytes fetched (J + K)
};

// Number of bus cycles needed to fetch `jk` instruction bytes.
inline constexpr unsigned fetch_bus_cycles(BusClass c, unsigned jk) {
  return bus_is_16bit(c) ? (jk + 1) / 2 : jk;
}

// Run-time condition that selects among the timing rows of one instruction.
enum class ExecCond : u8 {
  Normal,
  BranchTaken,      // Bcc/SCB/TRAP-VS taken
  BranchNotTaken,   // Bcc/SCB/TRAP-VS not taken
  ScbMinus1,        // SCB: count reached -1, loop exits
  DivZero,          // DIVXU zero divide (exception taken)
  DivOverflow,      // DIVXU overflow (division not performed)
};

struct TimingContext {
  bool max_mode = false;
  ExecCond cond = ExecCond::Normal;
  u8 n_regs = 0;    // LDM/STM register count
};

BaseTiming base_timing(const DecodedInsn& d, const TimingContext& ctx);

// Table A-8 / 2-9: adjustment depending on whether the instruction starts at an
// odd address.  Applies only when the instruction is fetched over a 16-bit bus.
unsigned parity_adjustment(const DecodedInsn& d, bool odd_start, ExecCond cond);

// Extra states for fetching J+K instruction bytes from a region of class `c`
// (2.6.1 / 2.6.4): +1 per word on a 16-bit 3-state bus, +1 per byte on an
// 8-bit 2-state bus, +2 per byte on an 8-bit 3-state bus.
inline constexpr unsigned fetch_penalty(BusClass c, unsigned jk) {
  switch (c) {
    case BusClass::W16_S2: return 0;
    case BusClass::W16_S3: return jk / 2;   // (J+K)/2, rounded down
    case BusClass::W8_S2:  return jk;
    case BusClass::W8_S3:  return 2 * jk;
  }
  return 0;
}

// Extra states for I operand bytes accessed in a region of class `c`.
inline constexpr unsigned operand_penalty(BusClass c, unsigned i, Size sz) {
  const bool word = (sz == Size::Word);
  switch (c) {
    case BusClass::W16_S2: return 0;
    case BusClass::W16_S3: return word ? i / 2 : i;
    case BusClass::W8_S2:  return word ? i : 0;
    case BusClass::W8_S3:  return word ? 2 * i : i;
  }
  return 0;
}

// Number of bus cycles implied by I operand bytes (for wait-state accounting).
inline constexpr unsigned operand_bus_cycles(BusClass c, unsigned i, Size sz) {
  if (sz == Size::Word && bus_is_16bit(c)) return (i + 1) / 2;
  return i;
}

}  // namespace h8500

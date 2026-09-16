// Instruction execution timing (MCS-96 user's manuals, instruction summary
// tables).  Times are in state times: two oscillator periods on the
// 80C196KB, three on the NMOS 8x9x.
//
// The tables give one figure per addressing mode, with a separate figure for
// operands in the register file ("internal") and operands reached through the
// memory controller ("external", address >= 0100h).  The decoder bakes both
// figures into a cell; the handler picks by the run-time address.
#pragma once
#include "cpu/mcs96/types.hpp"

namespace mcs96::timing {

enum class AddressMode : u8 {
  Direct,
  Immediate,
  Indirect,
  IndirectAutoIncrement,
  IndexedShort,
  IndexedLong,
};

enum class DataSpace : u8 { Internal, MemoryController };

enum class FixedInstruction : u8 { ClearCarry, SetCarry, DisableInterrupts, EnableInterrupts, ClearOverflowTrap, Nop, Skip, Reset };

constexpr u32 fixed(const Variant variant, const FixedInstruction instruction) {
  if (variant == Variant::I8x9x) return 4u;
  switch (instruction) {
    case FixedInstruction::Skip: return 3;
    case FixedInstruction::Reset: return 15;
    default: return 2;
  }
}

constexpr u32 conditionalBranch(const Variant, const bool taken) { return taken ? 8u : 4u; }
constexpr u32 bitBranch(const Variant, const bool taken) { return taken ? 9u : 5u; }
constexpr u32 jump(const Variant variant) { return variant == Variant::I8x9x ? 8u : 7u; }

constexpr u32 call(const Variant variant, const DataSpace stack) {
  if (variant == Variant::I8x9x) return stack == DataSpace::Internal ? 13u : 16u;
  return stack == DataSpace::Internal ? 11u : 13u;
}

constexpr u32 ret(const Variant variant, const DataSpace stack) {
  if (variant == Variant::I8x9x) return stack == DataSpace::Internal ? 12u : 16u;
  return stack == DataSpace::Internal ? 11u : 14u;
}

constexpr u32 interruptEntry(const Variant variant, const DataSpace stack) {
  if (variant == Variant::I8x9x) return stack == DataSpace::Internal ? 21u : 24u;
  return stack == DataSpace::Internal ? 16u : 18u;
}

// Timing shared by the ordinary ADD/SUB/ADDC/SUBC/CMP/AND/OR/XOR families,
// keyed on the addressing mode and the operand's data space.
constexpr u32 commonAlu(const Variant variant, const u8 operandCount, const bool byte, const AddressMode mode,
                        const DataSpace source) {
  const bool three = operandCount == 3;
  if (mode == AddressMode::Direct) return three ? 5u : 4u;
  if (mode == AddressMode::Immediate) return three ? (byte ? 5u : 6u) : (byte ? 4u : 5u);
  const u32 modeOffset = (mode == AddressMode::IndirectAutoIncrement || mode == AddressMode::IndexedLong) ? 1u : 0u;
  const u32 internal = (three ? 7u : 6u) + modeOffset;
  if (source == DataSpace::Internal) return internal;
  // The NMOS 8x9x pays five states to cross the memory controller for these
  // forms; the 80C196KB tables show three for three-operand forms, two for two.
  if (variant == Variant::I8x9x) return internal + 5u;
  return internal + (three ? 3u : 2u);
}

constexpr bool auto_increment(AddressMode m) { return m == AddressMode::IndirectAutoIncrement; }
constexpr bool long_indexed(AddressMode m) { return m == AddressMode::IndexedLong; }
constexpr bool memory_mode(AddressMode m) { return m >= AddressMode::Indirect; }

// MUL / MULU (unsigned: !prefixed) with a byte source.
constexpr u32 mulByte(Variant v, bool prefixed, bool three, AddressMode m, bool ext) {
  const bool increment = auto_increment(m) || long_indexed(m);
  const bool memory = memory_mode(m);
  if (v == Variant::I8x9x)
    return (three ? (prefixed ? 22u : 18u) : (prefixed ? 21u : 17u)) + (memory ? 2u + (increment ? 1u : 0u) : 0u);
  if (!memory) return prefixed ? 12 : 10;
  u32 t = 0;
  if (m == AddressMode::Indirect) t = prefixed ? 14 : 12;
  else if (m == AddressMode::IndirectAutoIncrement) t = prefixed ? (three ? 13 : 15) : (three ? 12 : 13);
  else if (m == AddressMode::IndexedShort) t = prefixed ? 15 : 12;
  else t = prefixed ? 16 : 14;
  if (ext) {
    if (prefixed) t += auto_increment(m) && three ? 2 : 3;
    else if (m == AddressMode::Indirect) t += 3;
    else if (m == AddressMode::IndirectAutoIncrement) t += three ? 4 : 2;
    else t += increment ? 3 : 4;
  }
  return t;
}

// MUL / MULU with a word source.
constexpr u32 mulWord(Variant v, bool prefixed, bool three, AddressMode m, bool ext) {
  const bool increment = auto_increment(m) || long_indexed(m);
  const bool memory = memory_mode(m);
  if (v == Variant::I8x9x)
    return (three ? (prefixed ? 30u : 26u) : (prefixed ? 29u : 25u)) +
           (m == AddressMode::Immediate ? 1u : memory ? 2u + (increment ? 1u : 0u) : 0u);
  if (!memory) return (prefixed ? 16u : 14u) + (m == AddressMode::Immediate ? 1u : 0u);
  u32 t = prefixed ? 18 : 16;
  if (m == AddressMode::IndirectAutoIncrement || m == AddressMode::IndexedShort) ++t;
  else if (m == AddressMode::IndexedLong) t += 2;
  if (ext) t += !prefixed && m == AddressMode::IndirectAutoIncrement ? 2 : 3;
  return t;
}

// DIV / DIVU with a byte divisor.
constexpr u32 divByte(Variant v, bool prefixed, AddressMode m, bool ext) {
  const bool increment = auto_increment(m) || long_indexed(m);
  const bool memory = memory_mode(m);
  if (v == Variant::I8x9x) return (prefixed ? 21u : 17u) + (memory ? 3u + (increment ? 1u : 0u) : 0u);
  u32 t = prefixed ? 18 : 16;
  if (memory)
    t += 2 + (increment ? 1 : 0) + ((m == AddressMode::IndexedShort || m == AddressMode::IndexedLong) ? 1 : 0) + (ext ? 3 : 0);
  return t;
}

// DIV / DIVU with a word divisor.
constexpr u32 divWord(Variant v, bool prefixed, AddressMode m, bool ext) {
  const bool increment = auto_increment(m) || long_indexed(m);
  const bool memory = memory_mode(m);
  if (v == Variant::I8x9x)
    return (prefixed ? 30u : 25u) +
           (m == AddressMode::Immediate ? (prefixed ? 0u : 1u) : memory ? (prefixed ? 2u : 3u) + (increment ? 1u : 0u) : 0u);
  u32 t = prefixed ? 26 : 24;
  if (m == AddressMode::Immediate) t += 1;
  else if (memory)
    t += 2 + (increment ? 1 : 0) + ((m == AddressMode::IndexedShort || m == AddressMode::IndexedLong) ? 1 : 0) + (ext ? 3 : 0);
  return t;
}

// LD / LDB / LDBZE / LDBSE.
constexpr u32 load(Variant v, bool byteLoad, AddressMode m, bool ext) {
  if (v == Variant::I8x9x) {
    switch (m) {
      case AddressMode::Direct: return 4;
      case AddressMode::Immediate: return byteLoad ? 4 : 5;
      case AddressMode::Indirect: return 6;
      case AddressMode::IndirectAutoIncrement: return 7;
      case AddressMode::IndexedShort: return 6;
      case AddressMode::IndexedLong: return 7;
    }
    return 0;
  }
  switch (m) {
    case AddressMode::Direct: return 4;
    case AddressMode::Immediate: return byteLoad ? 4 : 5;
    case AddressMode::Indirect: return ext ? 8 : 5;
    case AddressMode::IndirectAutoIncrement: return ext ? 8 : 6;
    case AddressMode::IndexedShort: return ext ? 9 : 6;
    case AddressMode::IndexedLong: return ext ? 10 : 7;
  }
  return 0;
}

// ST / STB to a memory operand.
constexpr u32 store(Variant v, AddressMode m, bool ext) {
  if (v == Variant::I8x9x) return (m == AddressMode::IndirectAutoIncrement || m == AddressMode::IndexedLong ? 8u : 7u) + (ext ? 5u : 0u);
  switch (m) {
    case AddressMode::Indirect: return ext ? 8 : 5;
    case AddressMode::IndirectAutoIncrement: return ext ? 9 : 6;
    case AddressMode::IndexedShort: return ext ? 9 : 6;
    default: return ext ? 10 : 7;
  }
}

}  // namespace mcs96::timing

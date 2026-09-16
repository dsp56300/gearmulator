// MCS-96 instruction decoder (8096 / 80C196KB user's manuals, instruction
// set).
//
// Decodes raw instruction bytes into a compact DecodedInsn: the handler kind,
// the addressing mode of the general operand (bits 1-0 of the opcode), the
// operand fields, the length and the state counts.  The decoder is a pure
// function of the bytes and the chip variant, so its output is cached per
// address by the CPU core.
#pragma once
#include <type_traits>

#include "cpu/mcs96/timing.hpp"
#include "cpu/mcs96/types.hpp"

namespace mcs96 {

using timing::AddressMode;

enum class Op : u8 {
  Illegal = 0,     // unimplemented opcode (interrupt on the KB)
  Skip,            // 00 xx
  Unary,           // CLR/NOT/NEG/DEC/INC (B): a=register, b=operation
  Ext,             // EXT / EXTB: a=register (aligned)
  Shift,           // SHR/SHL/SHRA (B/L): a=count, b=register, x=kind | width
  Norml,           // a=count register, b=long register
  Sjmp, Scall,     // imm=11-bit displacement
  Jbc, Jbs,        // a=register, b=bit, x=displacement
  Alu,             // 40-BF: general ALU / MUL / DIV / LD, see AluKind
  StDirect,        // ST/STB register,register: a=source, b=destination
  Bmov,            // KB: a=count register, b=pointer pair
  Cmpl,            // KB: a=source, b=destination
  St,              // ST/STB register to memory operand: b=source register
  PushDirect,      // a=register
  PushImm,         // imm
  PushMem,         // memory operand
  PopDirect,       // a=register
  PopMem,          // memory operand
  Jcond,           // D0-DF: b=condition, x=displacement
  Djnz, Djnzw,     // a=register, x=displacement
  BrInd,           // e3: a=register (aligned)
  Ljmp, Lcall,     // imm=16-bit displacement
  Ret, Pushf, Popf, Pusha, Popa,
  Idlpd,           // b=mode
  Trap, Clrc, Setc, Di, Ei, Clrvt, Nop, Rst,
  Count
};

// ALU family (opcodes 40h-BFh): the high nibble selects the family and the
// operand width, bits 3-2 the operation.
enum class AluKind : u8 {
  And, Add, Sub, Mul,            // 40-7F (three-operand forms at 40-5F)
  Or, Xor, Cmp, Div,             // 80-9F
  Ld, Addc, Subc, Ldbze, Ldbse,  // A0-BF
};

struct DecodedInsn {
  Op op = Op::Illegal;
  AluKind alu = AluKind::And;
  AddressMode mode = AddressMode::Direct;
  bool byte = false;      // byte-wide operation
  bool three = false;     // three-operand form
  bool prefixed = false;  // FEh prefix: signed MUL / DIV
  u8 len = 1;
  u8 a = 0, b = 0, x = 0;
  u16 imm = 0;
  u8 cyc = 0, cyc2 = 0;
  constexpr bool valid() const { return op != Op::Illegal; }
};

namespace detail {

template <class F>
inline DecodedInsn decode_impl(F& f, Variant v) {
  using namespace timing;
  DecodedInsn d;
  unsigned pos = 0;
  u8 op = f(pos++);
  if (op == 0xFE) {
    d.prefixed = true;
    op = f(pos++);
  }
  auto set = [&](Op o, u8 cyc = 0, u8 cyc2 = 0) { d.op = o; d.cyc = cyc; d.cyc2 = cyc2; };
  auto illegal = [&]() { d.op = Op::Illegal; d.cyc = 0; };
  const bool kb = v == Variant::I80C196KB;

  // General operand: addressing mode in bits 1-0.  Fills mode / a / imm.
  auto operand = [&](bool byteImm) {
    switch (op & 3) {
      case 0:
        d.mode = AddressMode::Direct;
        d.a = f(pos++);
        break;
      case 1:
        d.mode = AddressMode::Immediate;
        if (byteImm) d.imm = f(pos++);
        else { d.imm = u16(f(pos) | (u16(f(pos + 1)) << 8)); pos += 2; }
        break;
      case 2:
        d.a = f(pos++);
        d.mode = (d.a & 1) ? AddressMode::IndirectAutoIncrement : AddressMode::Indirect;
        break;
      default:
        d.a = f(pos++);
        if (d.a & 1) {
          d.imm = u16(f(pos) | (u16(f(pos + 1)) << 8));
          pos += 2;
          d.mode = AddressMode::IndexedLong;
        } else {
          d.imm = u16(s16(s8(f(pos++))));
          d.mode = AddressMode::IndexedShort;
        }
        break;
    }
  };

  if (op == 0x00) {
    pos += 1;
    set(Op::Skip, u8(fixed(v, FixedInstruction::Skip)));
  } else if (op >= 0x01 && op <= 0x1A && op != 0x04 && op != 0x0B && op != 0x10 && op != 0x14) {
    if (op == 0x08 || op == 0x09 || op == 0x0A || (op >= 0x0C && op <= 0x0F) || (op >= 0x18 && op <= 0x1A)) {
      if (op == 0x0F) {
        d.a = f(pos++);
        d.b = u8(f(pos++) & 0xFC);
        set(Op::Norml);
      } else {
        d.a = f(pos++);  // count: immediate if < 10h, else a register
        d.b = f(pos++);
        const bool byte = op >= 0x18, wide = op >= 0x0C && op <= 0x0E;
        d.x = u8((op & 3) | (byte ? 0x10 : 0) | (wide ? 0x20 : 0));
        set(Op::Shift);
      }
    } else {
      d.a = f(pos++);
      d.b = op & 0x0F;
      d.byte = (op & 0x10) != 0;
      if (d.b == 6) set(Op::Ext, 4);
      else set(Op::Unary, u8(v == Variant::I8x9x ? 4 : 3));
    }
  } else if (op >= 0x20 && op <= 0x3F) {
    if (op < 0x30) {
      u16 disp = u16(((op & 7) << 8) | f(pos++));
      if (disp & 0x0400) disp |= 0xFC00;
      d.imm = disp;
      if (op >= 0x28) set(Op::Scall, u8(call(v, DataSpace::Internal)), u8(call(v, DataSpace::MemoryController)));
      else set(Op::Sjmp, u8(jump(v)));
    } else {
      d.a = f(pos++);
      d.x = f(pos++);
      d.b = op & 7;
      set(op >= 0x38 ? Op::Jbs : Op::Jbc, u8(bitBranch(v, false)), u8(bitBranch(v, true)));
    }
  } else if (op >= 0x40 && op <= 0xBF) {
    const u8 high = op >> 4;
    const u8 operation = (op >> 2) & 3;
    d.byte = high == 5 || high == 7 || high == 9 || high == 0xB;
    d.three = high == 4 || high == 5;
    const bool byteSource = d.byte || (operation == 3 && high >= 9);
    operand(byteSource);
    d.b = f(pos++);                // destination register
    if (d.three) d.x = f(pos++);   // result register
    if (high <= 7) d.alu = AluKind(operation);                              // And Add Sub Mul
    else if (high <= 9) d.alu = AluKind(u8(AluKind::Or) + operation);      // Or Xor Cmp Div
    else if (operation == 3) d.alu = high == 0xA ? AluKind::Ldbze : AluKind::Ldbse;
    else d.alu = AluKind(u8(AluKind::Ld) + operation);                     // Ld Addc Subc
    set(Op::Alu);
    // Timing, register-file and memory-controller figures.
    switch (d.alu) {
      case AluKind::Mul:
        d.cyc = u8(d.byte ? mulByte(v, d.prefixed, d.three, d.mode, false) : mulWord(v, d.prefixed, d.three, d.mode, false));
        d.cyc2 = u8(d.byte ? mulByte(v, d.prefixed, d.three, d.mode, true) : mulWord(v, d.prefixed, d.three, d.mode, true));
        break;
      case AluKind::Div:
        d.cyc = u8(d.byte ? divByte(v, d.prefixed, d.mode, false) : divWord(v, d.prefixed, d.mode, false));
        d.cyc2 = u8(d.byte ? divByte(v, d.prefixed, d.mode, true) : divWord(v, d.prefixed, d.mode, true));
        break;
      case AluKind::Ld: case AluKind::Ldbze: case AluKind::Ldbse: {
        const bool byteLoad = high == 0xB || operation == 3;
        d.cyc = u8(load(v, byteLoad, d.mode, false));
        d.cyc2 = u8(load(v, byteLoad, d.mode, true));
        break;
      }
      default:
        d.cyc = u8(commonAlu(v, d.three ? 3 : 2, d.byte, d.mode, DataSpace::Internal));
        d.cyc2 = u8(commonAlu(v, d.three ? 3 : 2, d.byte, d.mode, DataSpace::MemoryController));
        break;
    }
  } else if (op >= 0xC0 && op <= 0xCF) {
    switch (op) {
      case 0xC0: case 0xC4:
        d.byte = op == 0xC4;
        d.b = f(pos++);  // destination
        d.a = f(pos++);  // source
        set(Op::StDirect, 4);
        break;
      case 0xC1:
        if (!kb) { illegal(); break; }
        d.a = f(pos++);
        d.b = u8(f(pos++) & 0xFC);
        set(Op::Bmov);
        break;
      case 0xC5:
        if (!kb) { illegal(); break; }
        d.a = u8(f(pos++) & 0xFC);
        d.b = u8(f(pos++) & 0xFC);
        set(Op::Cmpl, 7);
        break;
      case 0xC2: case 0xC3: case 0xC6: case 0xC7:
        d.byte = op >= 0xC6;
        operand(d.byte);
        d.b = f(pos++);
        set(Op::St, u8(store(v, d.mode, false)), u8(store(v, d.mode, true)));
        break;
      case 0xC8:
        d.a = f(pos++);
        set(Op::PushDirect, u8(v == Variant::I8x9x ? 8 : 6), u8(v == Variant::I8x9x ? 8 : 8));
        break;
      case 0xC9:
        d.imm = u16(f(pos) | (u16(f(pos + 1)) << 8));
        pos += 2;
        set(Op::PushImm, u8(v == Variant::I8x9x ? 8 : 7), u8(v == Variant::I8x9x ? 8 : 9));
        break;
      case 0xCA: case 0xCB:
        operand(false);
        set(Op::PushMem);
        break;
      case 0xCC:
        d.a = f(pos++);
        set(Op::PopDirect, u8(v == Variant::I8x9x ? 12 : 8), u8(v == Variant::I8x9x ? 12 : 11));
        break;
      case 0xCE: case 0xCF:
        operand(false);
        set(Op::PopMem);
        break;
      default:
        illegal();
        break;
    }
  } else if (op >= 0xD0 && op <= 0xDF) {
    d.b = op & 0x0F;
    d.x = f(pos++);
    set(Op::Jcond, u8(conditionalBranch(v, false)), u8(conditionalBranch(v, true)));
  } else {
    switch (op) {
      case 0xE0: case 0xE1:
        if (op == 0xE1 && !kb) { illegal(); break; }
        d.a = f(pos++);
        d.x = f(pos++);
        set(op == 0xE0 ? Op::Djnz : Op::Djnzw, 5, 9);
        break;
      case 0xE3: d.a = u8(f(pos++) & 0xFE); set(Op::BrInd, u8(jump(v))); break;
      case 0xE7:
        d.imm = u16(f(pos) | (u16(f(pos + 1)) << 8));
        pos += 2;
        set(Op::Ljmp, u8(jump(v)));
        break;
      case 0xEF:
        d.imm = u16(f(pos) | (u16(f(pos + 1)) << 8));
        pos += 2;
        set(Op::Lcall, u8(call(v, DataSpace::Internal)), u8(call(v, DataSpace::MemoryController)));
        break;
      case 0xF0: set(Op::Ret, u8(ret(v, DataSpace::Internal)), u8(ret(v, DataSpace::MemoryController))); break;
      case 0xF2: set(Op::Pushf, u8(v == Variant::I8x9x ? 8 : 6), u8(v == Variant::I8x9x ? 8 : 8)); break;
      case 0xF3: set(Op::Popf, u8(v == Variant::I8x9x ? 9 : 7), u8(v == Variant::I8x9x ? 9 : 10)); break;
      case 0xF4: if (!kb) { illegal(); break; } set(Op::Pusha, 12, 18); break;
      case 0xF5: if (!kb) { illegal(); break; } set(Op::Popa, 12, 18); break;
      case 0xF6: if (!kb) { illegal(); break; } d.b = f(pos++); set(Op::Idlpd, 8, 25); break;
      case 0xF7: set(Op::Trap); break;
      case 0xF8: set(Op::Clrc, u8(fixed(v, FixedInstruction::ClearCarry))); break;
      case 0xF9: set(Op::Setc, u8(fixed(v, FixedInstruction::SetCarry))); break;
      case 0xFA: set(Op::Di, u8(fixed(v, FixedInstruction::DisableInterrupts))); break;
      case 0xFB: set(Op::Ei, u8(fixed(v, FixedInstruction::EnableInterrupts))); break;
      case 0xFC: set(Op::Clrvt, u8(fixed(v, FixedInstruction::ClearOverflowTrap))); break;
      case 0xFD: set(Op::Nop, u8(fixed(v, FixedInstruction::Nop))); break;
      case 0xFF: set(Op::Rst, u8(fixed(v, FixedInstruction::Reset))); break;
      default: illegal(); break;
    }
  }
  d.len = u8(pos);
  return d;
}

}  // namespace detail

// Decode one instruction.  `fetch(offset)` must return the instruction byte at
// `offset` (0-based) relative to the instruction start; at most 7 bytes are read.
template <class Fetch>
  requires std::is_invocable_r_v<u8, Fetch&, unsigned>
inline DecodedInsn decode(Fetch&& fetch, Variant v) {
  return detail::decode_impl(fetch, v);
}

inline DecodedInsn decode(const u8* bytes, Variant v) {
  return decode([bytes](unsigned off) { return bytes[off]; }, v);
}

const char* op_name(Op op);

}  // namespace mcs96

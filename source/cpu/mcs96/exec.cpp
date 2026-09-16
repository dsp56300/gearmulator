// Instruction handlers ("threaded code") and instruction-cache fill.
//
// One template `h<Op>` covers the instruction set; the ALU family is
// instantiated per (kind, addressing mode, width, form) so operand access is
// resolved at compile time.  Semantics follow the MCS-96 user's manuals.
#include <algorithm>

#include "cpu/mcs96/cpu.hpp"

namespace mcs96 {

struct ExecImpl {
  using DataSpace = timing::DataSpace;

  static const Cell* seq(const Cell* ip) { return ip + ip->len; }
  static u16 pc_next(const Cpu& c, const Cell* ip) { return u16(c.pc_of(ip) + ip->len); }
  static const Cell* rel(Cpu& c, const Cell* ip, s16 disp) { return c.cell_at(u16(pc_next(c, ip) + disp)); }
  static bool flag(const Cpu& c, u16 f) { return (c.regs_.psw & f) != 0; }
  static void set_flag(Cpu& c, u16 f, bool on) {
    c.set_psw(u16(on ? (c.regs_.psw | f) : (c.regs_.psw & ~f)));
  }
  // Flag-only updates never touch INT_MASK or I, so they skip the re-evaluation.
  static void flags(Cpu& c, u16 clear, u16 set) { c.regs_.psw = u16((c.regs_.psw & ~clear) | set); }

  // ---------------------------------------------------------------------------
  // Arithmetic with flags

  static void set_nz8(Cpu& c, u8 v) {
    u16 set = 0;
    if (v == 0) set |= kZ; else if (s8(v) < 0) set |= kN;
    flags(c, kN | kV | kZ | kC, set);
  }
  static void set_nz16(Cpu& c, u16 v) {
    u16 set = 0;
    if (v == 0) set |= kZ; else if (s16(v) < 0) set |= kN;
    flags(c, kN | kV | kZ | kC, set);
  }
  template <bool Byte>
  static u32 add(Cpu& c, u32 left, u32 right, bool with_carry) {
    constexpr u32 msb = Byte ? 0x80 : 0x8000, mask = Byte ? 0xFF : 0xFFFF, carry_bit = Byte ? 0x100 : 0x10000;
    const u32 carry = with_carry && flag(c, kC) ? 1 : 0;
    const u32 sum = left + right + carry;
    const u32 result = sum & mask;
    const bool overflow = ((~(left ^ right) & (left ^ result)) & msb) != 0;
    u16 psw = u16(c.regs_.psw & ~(with_carry ? (kN | kC | kV) : (kN | kC | kV | kZ)));
    if (!with_carry && result == 0) psw |= kZ;
    else if (with_carry && result != 0) psw &= u16(~kZ);
    if (((result & msb) != 0) != overflow) psw |= kN;
    if (overflow) psw |= kV | kVt;
    if (sum & carry_bit) psw |= kC;
    c.regs_.psw = psw;
    return result;
  }
  template <bool Byte>
  static u32 sub(Cpu& c, u32 left, u32 right, bool with_carry) {
    constexpr u32 msb = Byte ? 0x80 : 0x8000, mask = Byte ? 0xFF : 0xFFFF, carry_bit = Byte ? 0x100 : 0x10000;
    const u32 borrow = with_carry && !flag(c, kC) ? 1 : 0;
    const u32 difference = left - right - borrow;
    const u32 result = difference & mask;
    const bool overflow = (((left ^ right) & (left ^ result)) & msb) != 0;
    u16 psw = u16(c.regs_.psw & ~(with_carry ? (kN | kC | kV) : (kN | kC | kV | kZ)));
    if (!with_carry && result == 0) psw |= kZ;
    else if (with_carry && result != 0) psw &= u16(~kZ);
    if (((result & msb) != 0) != overflow) psw |= kN;
    if (overflow) psw |= kV | kVt;
    if ((difference & carry_bit) == 0) psw |= kC;
    c.regs_.psw = psw;
    return result;
  }
  static u32 sub32(Cpu& c, u32 left, u32 right) {
    const u64 difference = u64(left) - right;
    const u32 result = u32(difference);
    const bool overflow = (((left ^ right) & (left ^ result)) & 0x80000000u) != 0;
    u16 psw = u16(c.regs_.psw & ~(kN | kV | kZ | kC));
    if (result == 0) psw |= kZ;
    else if ((s32(result) < 0) != overflow) psw |= kN;
    if (overflow) psw |= kV | kVt;
    if ((difference >> 32) == 0) psw |= kC;
    c.regs_.psw = psw;
    return result;
  }
  static void set_shift_left_overflow(Cpu& c, u32 value, unsigned width, unsigned count) {
    if (count == 0) return;
    bool overflow = false;
    if (count >= width) overflow = value != 0;
    else {
      const unsigned bits = count + 1;
      const u32 top = value >> (width - bits);
      const u32 all_ones = u32((u64(1) << bits) - 1);
      overflow = top != 0 && top != all_ones;
    }
    if (overflow) c.regs_.psw |= kV | kVt;
  }

  // ---------------------------------------------------------------------------
  // General operand (source of the ALU family, destination of ST / PUSH / POP)

  template <AddressMode M>
  static u16 operand_address(Cpu& c, const Cell* ip) {
    if constexpr (M == AddressMode::Direct) return ip->a;
    else if constexpr (M == AddressMode::Indirect || M == AddressMode::IndirectAutoIncrement) return c.reg16(ip->a);
    else {
      const u16 base = ip->a != 0 ? c.reg16(ip->a) : 0;
      return u16(base + ip->imm);
    }
  }
  template <AddressMode M, bool Byte>
  static u32 operand_read(Cpu& c, const Cell* ip, u16 address) {
    if constexpr (M == AddressMode::Immediate) return ip->imm;
    else if constexpr (M == AddressMode::Direct) return Byte ? c.reg8(ip->a) : c.reg16(ip->a);
    else return Byte ? c.read8(address) : c.read16(address);
  }
  template <AddressMode M, bool Byte>
  static void operand_finish(Cpu& c, const Cell* ip, u16 address) {
    if constexpr (M == AddressMode::IndirectAutoIncrement) c.set_reg16(ip->a, u16(address + (Byte ? 1 : 2)));
  }
  // States for an operand of the mode: the register-file figure or the
  // memory-controller one.
  template <AddressMode M>
  static unsigned operand_states(const Cell* ip, u16 address) {
    if constexpr (M == AddressMode::Direct || M == AddressMode::Immediate) return ip->cyc;
    else return Cpu::external(address) ? ip->cyc2 : ip->cyc;
  }

  // ---------------------------------------------------------------------------
  // ALU family

  template <AluKind K, AddressMode M, bool Byte, bool Three, bool Prefixed>
  static const Cell* h_alu(Cpu& c, const Cell* ip) {
    constexpr bool byteSource = Byte || K == AluKind::Ldbze || K == AluKind::Ldbse;
    const u16 address = operand_address<M>(c, ip);
    const u32 source = operand_read<M, byteSource>(c, ip, address);
    const u8 dest = ip->b;
    const unsigned states = operand_states<M>(ip, address);

    if constexpr (K == AluKind::And || K == AluKind::Add || K == AluKind::Sub) {
      const u8 result_reg = Three ? ip->x : dest;
      if constexpr (Byte) {
        const u8 right = u8(source), left = c.reg8(dest);
        u8 result;
        if constexpr (K == AluKind::And) { result = u8(left & right); set_nz8(c, result); }
        else if constexpr (K == AluKind::Add) result = u8(add<true>(c, left, right, false));
        else result = u8(sub<true>(c, left, right, false));
        c.set_reg8(result_reg, result);
      } else {
        const u16 right = u16(source), left = c.reg16(dest);
        u16 result;
        if constexpr (K == AluKind::And) { result = u16(left & right); set_nz16(c, result); }
        else if constexpr (K == AluKind::Add) result = u16(add<false>(c, left, right, false));
        else result = u16(sub<false>(c, left, right, false));
        c.set_reg16(result_reg, result);
      }
    } else if constexpr (K == AluKind::Mul) {
      const u8 result_reg = Three ? ip->x : dest;
      if constexpr (Byte) {
        const s32 result = Prefixed ? s32(s8(source)) * s32(s8(c.reg8(dest))) : s32(u8(source)) * s32(c.reg8(dest));
        c.set_reg16(u8(result_reg & 0xFE), u16(result));
      } else {
        const u32 result = Prefixed ? u32(s32(s16(source)) * s32(s16(c.reg16(dest)))) : u32(u16(source)) * c.reg16(dest);
        const u8 aligned = u8(result_reg & 0xFC);
        c.set_reg16(aligned, u16(result));
        c.set_reg16(u8(aligned + 2), u16(result >> 16));
      }
    } else if constexpr (K == AluKind::Or || K == AluKind::Xor || K == AluKind::Cmp) {
      if constexpr (Byte) {
        const u8 left = c.reg8(dest), right = u8(source);
        if constexpr (K == AluKind::Or) { const u8 r = u8(left | right); set_nz8(c, r); c.set_reg8(dest, r); }
        else if constexpr (K == AluKind::Xor) { const u8 r = u8(left ^ right); set_nz8(c, r); c.set_reg8(dest, r); }
        else sub<true>(c, left, right, false);
      } else {
        const u16 left = c.reg16(dest), right = u16(source);
        if constexpr (K == AluKind::Or) { const u16 r = u16(left | right); set_nz16(c, r); c.set_reg16(dest, r); }
        else if constexpr (K == AluKind::Xor) { const u16 r = u16(left ^ right); set_nz16(c, r); c.set_reg16(dest, r); }
        else sub<false>(c, left, right, false);
      }
    } else if constexpr (K == AluKind::Div) {
      const u8 aligned = Byte ? u8(dest & 0xFE) : u8(dest & 0xFC);
      c.regs_.psw &= u16(~kV);
      if (source == 0) {
        c.regs_.psw |= kV | kVt;
      } else if constexpr (Byte) {
        const u16 dividend = c.reg16(aligned);
        if constexpr (Prefixed) {
          const s32 quotient = s32(s16(dividend)) / s32(s8(source));
          const s32 remainder = s32(s16(dividend)) % s32(s8(source));
          if (quotient < -128 || quotient > 127) c.regs_.psw |= kV | kVt;
          c.set_reg16(aligned, u16((quotient & 0xFF) | ((remainder & 0xFF) << 8)));
        } else {
          const u32 quotient = dividend / u8(source);
          const u32 remainder = dividend % u8(source);
          if (quotient > 0xFF) c.regs_.psw |= kV | kVt;
          c.set_reg16(aligned, u16((quotient & 0xFF) | ((remainder & 0xFF) << 8)));
        }
      } else {
        const u32 dividend = c.reg16(aligned) | (u32(c.reg16(u8(aligned + 2))) << 16);
        u32 packed;
        if constexpr (Prefixed) {
          const s64 quotient = s64(s32(dividend)) / s64(s16(source));
          const s64 remainder = s64(s32(dividend)) % s64(s16(source));
          if (quotient < -32768 || quotient > 32767) c.regs_.psw |= kV | kVt;
          packed = (u32(quotient) & 0xFFFF) | ((u32(remainder) & 0xFFFF) << 16);
        } else {
          const u32 quotient = dividend / u16(source);
          const u32 remainder = dividend % u16(source);
          if (quotient > 0xFFFF) c.regs_.psw |= kV | kVt;
          packed = (quotient & 0xFFFF) | ((remainder & 0xFFFF) << 16);
        }
        c.set_reg16(aligned, u16(packed));
        c.set_reg16(u8(aligned + 2), u16(packed >> 16));
      }
    } else if constexpr (K == AluKind::Ld) {
      if constexpr (Byte) c.set_reg8(dest, u8(source)); else c.set_reg16(dest, u16(source));
    } else if constexpr (K == AluKind::Addc) {
      if constexpr (Byte) c.set_reg8(dest, u8(add<true>(c, c.reg8(dest), u8(source), true)));
      else c.set_reg16(dest, u16(add<false>(c, c.reg16(dest), u16(source), true)));
    } else if constexpr (K == AluKind::Subc) {
      if constexpr (Byte) c.set_reg8(dest, u8(sub<true>(c, c.reg8(dest), u8(source), true)));
      else c.set_reg16(dest, u16(sub<false>(c, c.reg16(dest), u16(source), true)));
    } else if constexpr (K == AluKind::Ldbze) {
      c.set_reg16(dest, u8(source));
    } else {
      static_assert(K == AluKind::Ldbse);
      c.set_reg16(dest, u16(s16(s8(source))));
    }
    operand_finish<M, byteSource>(c, ip, address);
    EMU_END(c, seq(ip), states);
  }

  // ---------------------------------------------------------------------------
  // Everything else

  template <Op O, AddressMode M, Variant V>
  static const Cell* h(Cpu& c, const Cell* ip) {
    Cpu::Regs& R = c.regs_;
    [[maybe_unused]] constexpr bool kb = V == Variant::I80C196KB;

    if constexpr (O == Op::Illegal) {
      ++c.illegal_count_;
      if constexpr (kb) {
        // Unimplemented opcode interrupt: the return address is the byte after
        // the opcode.
        c.push16(u16(c.pc_of(ip) + 1));
        const DataSpace stack = Cpu::external(c.sp()) ? DataSpace::MemoryController : DataSpace::Internal;
        c.hold_off();
        const u16 target = c.read16(kUnimplementedOpcodeVector);
        EMU_END(c, c.cell_at(target), timing::interruptEntry(V, stack));
      } else {
        EMU_END(c, ip + 1, 4);
      }
    } else if constexpr (O == Op::Skip || O == Op::Nop || O == Op::Clrc || O == Op::Setc || O == Op::Clrvt) {
      if constexpr (O == Op::Clrc) flags(c, kC, 0);
      else if constexpr (O == Op::Setc) flags(c, 0, kC);
      else if constexpr (O == Op::Clrvt) flags(c, kVt, 0);
      EMU_END(c, seq(ip), ip->cyc);
    } else if constexpr (O == Op::Di || O == Op::Ei) {
      set_flag(c, kI, O == Op::Ei);
      c.hold_off();
      EMU_END(c, seq(ip), ip->cyc);

    } else if constexpr (O == Op::Unary) {
      const u8 address = ip->a;
      const u8 operation = ip->b;
      if (ip->x) {  // byte (x carries the width flag: see fill)
        const u8 value = c.reg8(address);
        u8 result = value;
        switch (operation) {
          case 1: result = 0; set_nz8(c, result); break;
          case 2: result = u8(~value); set_nz8(c, result); break;
          case 3: result = u8(sub<true>(c, 0, value, false)); break;
          case 5: result = u8(sub<true>(c, value, 1, false)); break;
          case 7: result = u8(add<true>(c, value, 1, false)); break;
          default: break;
        }
        c.set_reg8(address, result);
      } else {
        const u16 value = c.reg16(address);
        u16 result = value;
        switch (operation) {
          case 1: result = 0; set_nz16(c, result); break;
          case 2: result = u16(~value); set_nz16(c, result); break;
          case 3: result = u16(sub<false>(c, 0, value, false)); break;
          case 5: result = u16(sub<false>(c, value, 1, false)); break;
          case 7: result = u16(add<false>(c, value, 1, false)); break;
          default: break;
        }
        c.set_reg16(address, result);
      }
      EMU_END(c, seq(ip), ip->cyc);
    } else if constexpr (O == Op::Ext) {
      if (ip->x) {  // EXTB
        const u8 address = u8(ip->a & 0xFE);
        const s16 value = s8(c.reg8(address));
        set_nz8(c, u8(value));
        c.set_reg16(address, u16(value));
      } else {
        const u8 address = u8(ip->a & 0xFC);
        const s32 value = s16(c.reg16(address));
        set_nz16(c, u16(value));
        c.set_reg16(u8(address + 2), u16(u32(value) >> 16));
      }
      EMU_END(c, seq(ip), ip->cyc);

    } else if constexpr (O == Op::Shift) {
      u8 count = ip->a;
      const u8 destination = ip->b;
      if (count >= 0x10) count = u8(c.reg8(count) & 0x1F);
      const bool byte = (ip->x & 0x10) != 0, wide = (ip->x & 0x20) != 0;
      const unsigned width = wide ? 32u : (byte ? 8u : 16u);
      const u8 kind = ip->x & 3;
      u32 value = byte ? c.reg8(destination) : c.reg16(destination);
      if (wide) {
        const u8 aligned = u8(destination & 0xFC);
        value = c.reg16(aligned) | (u32(c.reg16(u8(aligned + 2))) << 16);
      }
      const u32 mask = width == 32 ? 0xFFFFFFFFu : u32((u64(1) << width) - 1);
      R.psw &= u16(~(kZ | kN | kC | kV | kSt));
      if (kind == 1) {
        if (count >= 1 && count <= width && (value & (u32(1) << (width - count))) != 0) R.psw |= kC;
        set_shift_left_overflow(c, value, width, count);
        value = count >= width ? 0 : u32((value << count) & mask);
      } else {
        if (count >= 2) {
          const unsigned sticky_bits = count > width ? width : count - 1;
          const u32 sticky_mask = sticky_bits == 32 ? 0xFFFFFFFFu : u32((u64(1) << sticky_bits) - 1);
          if ((value & sticky_mask) != 0) R.psw |= kSt;
        }
        if (count >= 1 && count <= width && (value & (u32(1) << (count - 1))) != 0) R.psw |= kC;
        if (kind == 2) {  // arithmetic
          const bool negative = (value & (u32(1) << (width - 1))) != 0;
          if (count > width && negative) R.psw |= kC;
          if (count >= width) value = negative ? mask : 0;
          else if (negative && count != 0) value = u32((value >> count) | (mask << (width - count)));
          else value >>= count;
        } else {
          value = count >= width ? 0 : value >> count;
        }
      }
      value &= mask;
      if (value == 0) R.psw |= kZ;
      else if ((value & (u32(1) << (width - 1))) != 0) R.psw |= kN;
      if (byte) c.set_reg8(destination, u8(value));
      else {
        const u8 aligned = wide ? u8(destination & 0xFC) : destination;
        c.set_reg16(aligned, u16(value));
        if (wide) c.set_reg16(u8(aligned + 2), u16(value >> 16));
      }
      unsigned states;
      if (wide) states = count != 0 ? 7 + count : 8;
      else states = V == Variant::I8x9x ? (count != 0 ? 7 + count : 8) : (count != 0 ? 6 + count : 7);
      EMU_END(c, seq(ip), states);
    } else if constexpr (O == Op::Norml) {
      const u8 count_address = ip->a, value_address = ip->b;
      u32 value = c.reg16(value_address) | (u32(c.reg16(u8(value_address + 2))) << 16);
      u8 count = 0;
      while (count < 31 && s32(value) >= 0) { value <<= 1; ++count; }
      R.psw &= u16(~(kZ | kN | kC));
      if (value == 0) R.psw |= kZ;
      else if (s32(value) < 0) R.psw |= kN;
      c.set_reg8(count_address, count);
      c.set_reg16(value_address, u16(value));
      c.set_reg16(u8(value_address + 2), u16(value >> 16));
      EMU_END(c, seq(ip), V == Variant::I8x9x ? 11u + count : (count != 0 ? 8u + count : 9u));

    } else if constexpr (O == Op::Sjmp || O == Op::Ljmp) {
      EMU_END(c, rel(c, ip, s16(ip->imm)), ip->cyc);
    } else if constexpr (O == Op::Scall || O == Op::Lcall) {
      const u16 ret = pc_next(c, ip);
      c.push16(ret);
      const bool ext = Cpu::external(c.sp());
      EMU_END(c, c.cell_at(u16(ret + s16(ip->imm))), ext ? ip->cyc2 : ip->cyc);
    } else if constexpr (O == Op::Jbc || O == Op::Jbs) {
      const bool set = (c.reg8(ip->a) & (1u << ip->b)) != 0;
      const bool taken = O == Op::Jbs ? set : !set;
      if (taken) EMU_END(c, rel(c, ip, s8(ip->x)), ip->cyc2);
      EMU_END(c, seq(ip), ip->cyc);
    } else if constexpr (O == Op::Jcond) {
      bool taken = false;
      switch (ip->b) {
        case 0: taken = !flag(c, kSt); break;
        case 1: taken = !flag(c, kC) || flag(c, kZ); break;
        case 2: taken = !flag(c, kN) && !flag(c, kZ); break;
        case 3: taken = !flag(c, kC); break;
        case 4: taken = !flag(c, kVt); if (!taken) flags(c, kVt, 0); break;
        case 5: taken = !flag(c, kV); break;
        case 6: taken = !flag(c, kN); break;
        case 7: taken = !flag(c, kZ); break;
        case 8: taken = flag(c, kSt); break;
        case 9: taken = flag(c, kC) && !flag(c, kZ); break;
        case 10: taken = flag(c, kN) || flag(c, kZ); break;
        case 11: taken = flag(c, kC); break;
        case 12: taken = flag(c, kVt); if (taken) flags(c, kVt, 0); break;
        case 13: taken = flag(c, kV); break;
        case 14: taken = flag(c, kN); break;
        default: taken = flag(c, kZ); break;
      }
      if (taken) EMU_END(c, rel(c, ip, s8(ip->x)), ip->cyc2);
      EMU_END(c, seq(ip), ip->cyc);
    } else if constexpr (O == Op::Djnz || O == Op::Djnzw) {
      u16 result;
      if constexpr (O == Op::Djnz) { result = u8(c.reg8(ip->a) - 1); c.set_reg8(ip->a, u8(result)); }
      else { result = u16(c.reg16(ip->a) - 1); c.set_reg16(ip->a, result); }
      if (result != 0) EMU_END(c, rel(c, ip, s8(ip->x)), ip->cyc2);
      EMU_END(c, seq(ip), ip->cyc);
    } else if constexpr (O == Op::BrInd) {
      EMU_END(c, c.cell_at(c.reg16(ip->a)), ip->cyc);
    } else if constexpr (O == Op::Ret) {
      const bool ext = Cpu::external(c.sp());
      const u16 target = c.pop16();
      EMU_END(c, c.cell_at(target), ext ? ip->cyc2 : ip->cyc);

    } else if constexpr (O == Op::StDirect) {
      if (ip->x) c.set_reg8(ip->b, c.reg8(ip->a)); else c.set_reg16(ip->b, c.reg16(ip->a));
      EMU_END(c, seq(ip), ip->cyc);
    } else if constexpr (O == Op::St) {
      const bool byte = ip->x != 0;
      const u16 address = operand_address<M>(c, ip);
      if (byte) c.write8(address, c.reg8(ip->b)); else c.write16(address, c.reg16(ip->b));
      if (byte) operand_finish<M, true>(c, ip, address); else operand_finish<M, false>(c, ip, address);
      EMU_END(c, seq(ip), operand_states<M>(ip, address));
    } else if constexpr (O == Op::Bmov) {
      u32 count = c.reg16(ip->a);
      if (count == 0) count = 0x10000;
      u16 source = c.reg16(ip->b);
      u16 destination = c.reg16(u8(ip->b + 2));
      unsigned states = 6;
      while (count-- != 0) {
        c.write16(destination, c.read16(source));
        states += source < 0x100 ? (destination < 0x100 ? 8 : 11) : (destination < 0x100 ? 11 : 14);
        source = u16(source + 2);
        destination = u16(destination + 2);
      }
      c.set_reg16(ip->b, source);
      c.set_reg16(u8(ip->b + 2), destination);
      EMU_END(c, seq(ip), states);
    } else if constexpr (O == Op::Cmpl) {
      const u32 right = c.reg16(ip->a) | (u32(c.reg16(u8(ip->a + 2))) << 16);
      const u32 left = c.reg16(ip->b) | (u32(c.reg16(u8(ip->b + 2))) << 16);
      sub32(c, left, right);
      EMU_END(c, seq(ip), ip->cyc);

    } else if constexpr (O == Op::PushDirect) {
      const u16 old_sp = c.sp();
      c.set_sp(u16(old_sp - 2));
      // The NMOS part pushes the pre-decrement SP when SP itself is pushed.
      const u16 value = (V == Variant::I8x9x && (ip->a & 0xFE) == kSpRegister) ? old_sp : c.reg16(ip->a);
      c.write16(c.sp(), value);
      EMU_END(c, seq(ip), Cpu::external(c.sp()) ? ip->cyc2 : ip->cyc);
    } else if constexpr (O == Op::PushImm) {
      c.set_sp(u16(c.sp() - 2));
      c.write16(c.sp(), ip->imm);
      EMU_END(c, seq(ip), Cpu::external(c.sp()) ? ip->cyc2 : ip->cyc);
    } else if constexpr (O == Op::PushMem) {
      u16 address = operand_address<M>(c, ip);
      c.set_sp(u16(c.sp() - 2));
      // The KB reads through the already decremented SP when SP is the pointer.
      if constexpr (kb) if ((ip->a & 0xFE) == kSpRegister) address = u16(address - 2);
      const u16 value = c.read16(address);
      c.write16(c.sp(), value);
      operand_finish<M, false>(c, ip, address);
      const bool ext_stack = Cpu::external(c.sp()), ext_operand = Cpu::external(address);
      unsigned states;
      if constexpr (V == Variant::I8x9x) states = (M == AddressMode::IndirectAutoIncrement || M == AddressMode::IndexedLong) ? 12 : 11;
      else {
        constexpr bool indirect = M == AddressMode::Indirect || M == AddressMode::IndirectAutoIncrement;
        states = (indirect ? (M == AddressMode::IndirectAutoIncrement ? 10u : 9u) : (M == AddressMode::IndexedLong ? 11u : 10u)) +
                 (ext_stack ? 2u : 0u) + (ext_operand ? 3u : 0u);
      }
      EMU_END(c, seq(ip), states);
    } else if constexpr (O == Op::PopDirect) {
      const u16 old_sp = c.sp();
      const bool ext = Cpu::external(old_sp);
      c.set_sp(u16(old_sp + 2));
      const u16 value = c.read16(old_sp);
      c.set_reg16(ip->a, value);
      EMU_END(c, seq(ip), ext ? ip->cyc2 : ip->cyc);
    } else if constexpr (O == Op::PopMem) {
      u16 address = operand_address<M>(c, ip);
      const u16 old_sp = c.sp();
      const bool ext_stack = Cpu::external(old_sp);
      c.set_sp(u16(old_sp + 2));
      const u16 value = c.read16(old_sp);
      if constexpr (kb) if ((ip->a & 0xFE) == kSpRegister) address = u16(address + 2);
      const bool ext_operand = Cpu::external(address);
      c.write16(address, value);
      operand_finish<M, false>(c, ip, address);
      unsigned states;
      if constexpr (V == Variant::I8x9x) states = 14 + (ext_operand ? 5 : 0);
      else {
        constexpr bool indirect = M == AddressMode::Indirect || M == AddressMode::IndirectAutoIncrement;
        states = (indirect ? (M == AddressMode::IndirectAutoIncrement ? 11u : 10u) : (M == AddressMode::IndexedLong ? 12u : 11u)) +
                 (ext_stack ? 3u : 0u) + (ext_operand ? 2u : 0u);
      }
      EMU_END(c, seq(ip), states);

    } else if constexpr (O == Op::Pushf) {
      c.push16(R.psw);
      const bool ext = Cpu::external(c.sp());
      c.set_psw(0);
      c.hold_off();
      EMU_END(c, seq(ip), ext ? ip->cyc2 : ip->cyc);
    } else if constexpr (O == Op::Popf) {
      const bool ext = Cpu::external(c.sp());
      c.set_psw(c.pop16());
      c.hold_off();
      EMU_END(c, seq(ip), ext ? ip->cyc2 : ip->cyc);
    } else if constexpr (O == Op::Pusha) {
      const bool ext = Cpu::external(c.sp());
      c.push16(R.psw);
      c.set_psw(0);
      c.push16(c.sfr_ ? c.sfr_->aux_psw() : 0);
      if (c.sfr_) c.sfr_->set_aux_psw(0);
      c.hold_off();
      EMU_END(c, seq(ip), ext ? ip->cyc2 : ip->cyc);
    } else if constexpr (O == Op::Popa) {
      const bool ext = Cpu::external(c.sp());
      const u16 aux = c.pop16();
      if (c.sfr_) c.sfr_->set_aux_psw(aux);
      c.set_psw(c.pop16());
      c.hold_off();
      EMU_END(c, seq(ip), ext ? ip->cyc2 : ip->cyc);
    } else if constexpr (O == Op::Idlpd) {
      const u8 mode = ip->b;
      if (mode == 1 || mode == 2) {
        c.sleep(mode == 2);
        EMU_END(c, seq(ip), ip->cyc);
      }
      c.reset_architectural_state();
      if (c.sfr_) c.sfr_->reset_sfrs();
      c.hold_off();
      EMU_END(c, c.cell_at(R.pc), ip->cyc2);
    } else if constexpr (O == Op::Trap) {
      c.hold_off();
      SfrBlock::Interrupt irq;
      irq.vector = kSoftwareTrapVector;
      irq.maskable = false;
      R.pc = pc_next(c, ip);
      const unsigned states = c.enter_interrupt(irq);
      EMU_END(c, c.cell_at(R.pc), states);
    } else if constexpr (O == Op::Rst) {
      c.reset_architectural_state();
      if (c.sfr_) c.sfr_->reset_sfrs();
      c.reeval_irq();
      EMU_END(c, c.cell_at(R.pc), ip->cyc);
    } else {
      static_assert(O == Op::Alu, "unhandled Op");
      EMU_END(c, seq(ip), 0);
    }
  }

  // ---------------------------------------------------------------------------
  // Handler selection and cell fill

  template <AluKind K, AddressMode M, bool Byte, bool Three>
  static Handler pick_alu_p(bool prefixed) {
    return prefixed ? &h_alu<K, M, Byte, Three, true> : &h_alu<K, M, Byte, Three, false>;
  }
  template <AluKind K, AddressMode M, bool Byte>
  static Handler pick_alu_t(bool three, bool prefixed) {
    return three ? pick_alu_p<K, M, Byte, true>(prefixed) : pick_alu_p<K, M, Byte, false>(prefixed);
  }
  template <AluKind K, AddressMode M>
  static Handler pick_alu_b(const DecodedInsn& d) {
    return d.byte ? pick_alu_t<K, M, true>(d.three, d.prefixed) : pick_alu_t<K, M, false>(d.three, d.prefixed);
  }
  template <AluKind K>
  static Handler pick_alu(const DecodedInsn& d) {
    switch (d.mode) {
      case AddressMode::Direct: return pick_alu_b<K, AddressMode::Direct>(d);
      case AddressMode::Immediate: return pick_alu_b<K, AddressMode::Immediate>(d);
      case AddressMode::Indirect: return pick_alu_b<K, AddressMode::Indirect>(d);
      case AddressMode::IndirectAutoIncrement: return pick_alu_b<K, AddressMode::IndirectAutoIncrement>(d);
      case AddressMode::IndexedShort: return pick_alu_b<K, AddressMode::IndexedShort>(d);
      default: return pick_alu_b<K, AddressMode::IndexedLong>(d);
    }
  }
  template <Op O, Variant V>
  static Handler pick_mode(AddressMode m) {
    switch (m) {
      case AddressMode::Direct: return &h<O, AddressMode::Direct, V>;
      case AddressMode::Immediate: return &h<O, AddressMode::Immediate, V>;
      case AddressMode::Indirect: return &h<O, AddressMode::Indirect, V>;
      case AddressMode::IndirectAutoIncrement: return &h<O, AddressMode::IndirectAutoIncrement, V>;
      case AddressMode::IndexedShort: return &h<O, AddressMode::IndexedShort, V>;
      default: return &h<O, AddressMode::IndexedLong, V>;
    }
  }
  template <Variant V>
  static Handler select_v(const DecodedInsn& d) {
    switch (d.op) {
      case Op::Alu:
        switch (d.alu) {
          case AluKind::And: return pick_alu<AluKind::And>(d);
          case AluKind::Add: return pick_alu<AluKind::Add>(d);
          case AluKind::Sub: return pick_alu<AluKind::Sub>(d);
          case AluKind::Mul: return pick_alu<AluKind::Mul>(d);
          case AluKind::Or: return pick_alu<AluKind::Or>(d);
          case AluKind::Xor: return pick_alu<AluKind::Xor>(d);
          case AluKind::Cmp: return pick_alu<AluKind::Cmp>(d);
          case AluKind::Div: return pick_alu<AluKind::Div>(d);
          case AluKind::Ld: return pick_alu<AluKind::Ld>(d);
          case AluKind::Addc: return pick_alu<AluKind::Addc>(d);
          case AluKind::Subc: return pick_alu<AluKind::Subc>(d);
          case AluKind::Ldbze: return pick_alu<AluKind::Ldbze>(d);
          case AluKind::Ldbse: return pick_alu<AluKind::Ldbse>(d);
        }
        return &h<Op::Illegal, AddressMode::Direct, V>;
      case Op::St: return pick_mode<Op::St, V>(d.mode);
      case Op::PushMem: return pick_mode<Op::PushMem, V>(d.mode);
      case Op::PopMem: return pick_mode<Op::PopMem, V>(d.mode);
#define MCS96_OP(name) case Op::name: return &h<Op::name, AddressMode::Direct, V>;
      MCS96_OP(Illegal) MCS96_OP(Skip) MCS96_OP(Unary) MCS96_OP(Ext) MCS96_OP(Shift) MCS96_OP(Norml)
      MCS96_OP(Sjmp) MCS96_OP(Scall) MCS96_OP(Jbc) MCS96_OP(Jbs) MCS96_OP(StDirect) MCS96_OP(Bmov) MCS96_OP(Cmpl)
      MCS96_OP(PushDirect) MCS96_OP(PushImm) MCS96_OP(PopDirect) MCS96_OP(Jcond) MCS96_OP(Djnz) MCS96_OP(Djnzw)
      MCS96_OP(BrInd) MCS96_OP(Ljmp) MCS96_OP(Lcall) MCS96_OP(Ret) MCS96_OP(Pushf) MCS96_OP(Popf) MCS96_OP(Pusha)
      MCS96_OP(Popa) MCS96_OP(Idlpd) MCS96_OP(Trap) MCS96_OP(Clrc) MCS96_OP(Setc) MCS96_OP(Di) MCS96_OP(Ei)
      MCS96_OP(Clrvt) MCS96_OP(Nop) MCS96_OP(Rst)
#undef MCS96_OP
      case Op::Count: break;
    }
    return &h<Op::Illegal, AddressMode::Direct, V>;
  }
  static Handler select(const DecodedInsn& d, Variant v) {
    return v == Variant::I80C196KB ? select_v<Variant::I80C196KB>(d) : select_v<Variant::I8x9x>(d);
  }

  // Guard cell past a page end: continue in the page that holds the address.
  static const Cell* cross(Cpu& c, const Cell* ip) {
    const Cell* next = c.cells_for(c.pc_of(ip));
    EMU_GOTO(c, next);
  }

  static const Cell* fill(Cpu& c, const Cell* ip) {
    const u16 pc = c.pc_of(ip);
    const DecodedInsn d = c.decode_at(pc);
    Cell cell{};
    cell.fn = select(d, c.variant_);
    cell.imm = d.imm;
    cell.a = d.a;
    cell.b = d.b;
    cell.x = d.x;
    cell.len = d.len;
    cell.cyc = d.cyc;
    cell.cyc2 = d.cyc2;
    // Width flag for the handlers that are not instantiated per width.
    if (d.op == Op::Unary || d.op == Op::Ext || d.op == Op::StDirect || d.op == Op::St) cell.x = d.byte ? 1 : 0;
    c.bus_.mark_code(pc);
    c.bus_.mark_code(u16(pc + d.len - 1));
    *const_cast<Cell*>(ip) = cell;
    EMU_GOTO(c, ip);
  }
};

namespace detail {
const Cell* cell_fill(Cpu& cpu, const Cell* ip) { EMU_TAILCALL(ExecImpl::fill(cpu, ip)); }
const Cell* cell_cross(Cpu& cpu, const Cell* ip) { EMU_TAILCALL(ExecImpl::cross(cpu, ip)); }
}  // namespace detail

const char* op_name(Op op) {
  switch (op) {
#define MCS96_OP(name) case Op::name: return #name;
    MCS96_OP(Illegal) MCS96_OP(Skip) MCS96_OP(Unary) MCS96_OP(Ext) MCS96_OP(Shift) MCS96_OP(Norml)
    MCS96_OP(Sjmp) MCS96_OP(Scall) MCS96_OP(Jbc) MCS96_OP(Jbs) MCS96_OP(Alu) MCS96_OP(StDirect) MCS96_OP(Bmov) MCS96_OP(Cmpl)
    MCS96_OP(St) MCS96_OP(PushDirect) MCS96_OP(PushImm) MCS96_OP(PushMem) MCS96_OP(PopDirect) MCS96_OP(PopMem)
    MCS96_OP(Jcond) MCS96_OP(Djnz) MCS96_OP(Djnzw) MCS96_OP(BrInd) MCS96_OP(Ljmp) MCS96_OP(Lcall) MCS96_OP(Ret)
    MCS96_OP(Pushf) MCS96_OP(Popf) MCS96_OP(Pusha) MCS96_OP(Popa) MCS96_OP(Idlpd) MCS96_OP(Trap) MCS96_OP(Clrc)
    MCS96_OP(Setc) MCS96_OP(Di) MCS96_OP(Ei) MCS96_OP(Clrvt) MCS96_OP(Nop) MCS96_OP(Rst)
#undef MCS96_OP
    case Op::Count: break;
  }
  return "?";
}

}  // namespace mcs96

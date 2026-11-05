// MC6801 / HD6301 / MC68HC11 instruction decoder: one DecodedInsn per
// instruction, shared by the cache fill (exec.cpp) and the disassembler.
//
// Cycle counts are the manuals' (MC6801 reference manual appendix F,
// HD6301V1 manual table 3-3-1, the M68HC11 reference manual's instruction
// tables). The data-access cycles come from the same
// cycle-by-cycle tables: the MC6801 counts the opcode fetch as cycle 1, the
// HD6301 fetches the next opcode during the last cycle of the previous
// instruction, so the same access lands one or two cycles earlier on it.
//
// Undefined opcodes: the HD6301 traps (vector $FFEE, the interrupt
// sequence); $12 and $13 are its undocumented "add the byte at S+1 to X".
// What the MC6801 does on one is not documented; it is a one-byte no-op of
// four cycles here (as in MAME), except the store-immediate forms ($87, $8F,
// $C7, $CD, $CF), which store the register over their own operand bytes.
//
// The HC11 runs the MC6801's instructions in the MC6801's cycles, except
// RTI, SWI and WAI, which stack Y as well. A prebyte ($18, $1A, $CD) costs
// one cycle more and moves every bus cycle of the instruction one later:
// page $18 turns the X instructions into their Y forms and indexed-X
// addressing into indexed-Y; $1A holds CPD and the indexed-X forms of
// CPY / LDY / STY, $CD the indexed-Y forms of CPD / CPX / LDX / STX. An
// opcode outside the pages (TEST included, the normal modes have none)
// takes the illegal-opcode trap.
#pragma once
#include "cpu/m6801/types.hpp"

namespace m6801 {

enum class Mode : u8 { Inh, Imm, Dir, Idx, Ext, Rel, Idy };

enum class Op : u8 {
  Illegal, Trap,
  Nop, Lsrd, Asld, Tap, Tpa, Inx, Dex, Clv, Sev, Clc, Sec, Cli, Sei,
  Sba, Cba, Undoc, Tab, Tba, Xgdx, Daa, Slp, Aba,
  Bcc,  // cond in DecodedInsn::cond
  Tsx, Ins, Pul, Des, Txs, Psh, Pulx, Rts, Abx, Rti, Pshx, Mul, Wai, Swi,
  // read-modify-write: accumulator (reg) or memory (Idx / Ext)
  Neg, Com, Lsr, Ror, Asr, Asl, Rol, Dec, Inc, Tst, Clr,
  Aim, Oim, Eim, Tim,  // HD6301: imm8 in x, Dir / Idx
  Jmp, Jsr, Bsr,
  // accumulator A / B (reg) with an 8-bit operand
  Sub, Cmp, Sbc, And, Bit, Lda, Sta, Eor, Adc, Ora, Add,
  // 16-bit
  Subd, Addd, Cpx, Ldd, Ldx, Lds, Std, Stx, Sts,
  // HC11. The X instructions (INX, DEX, TSX, TXS, ABX, PSHX, PULX, XGDX,
  // CPX, LDX, STX) carry the index register in DecodedInsn::reg (1 = Y).
  Cpd, Idiv, Fdiv, Bset, Bclr, Brset, Brclr, Stop,
};

struct DecodedInsn {
  Op op = Op::Illegal;
  Mode mode = Mode::Inh;
  u8 opcode = 0;
  u8 page = 0;   // HC11 prebyte ($18, $1A, $CD), 0 for none
  u8 reg = 0;    // 0 = A, 1 = B; for the index-register instructions 0 = X, 1 = Y
  u8 cond = 0;   // Bcc condition (low nibble of the opcode)
  u8 len = 1;    // bytes
  u8 cyc = 4;    // E cycles
  u8 d0 = 0;     // cycle (0-based) of the first data access
  u8 d1 = 0;     // cycle of the read-modify-write store / the SWI vector fetch
  u8 x = 0;      // AIM/OIM/EIM/TIM immediate, BSET/BCLR/BRSET/BRCLR mask
  u8 ea = 0;     // BRSET/BRCLR direct address or index offset
  u16 imm = 0;   // immediate, address, index offset or branch target
  bool valid() const { return op != Op::Illegal && op != Op::Trap; }
};

// E cycles per opcode. Undefined opcodes: four cycles on the MC6801, the
// trap sequence (twelve, as an interrupt) on the HD6301.
inline constexpr u8 kCycles6801[256] = {
    4, 2, 4, 4, 3, 3, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2,  // 0
    2, 2, 4, 4, 4, 4, 2, 2, 4, 2, 4, 2, 4, 4, 4, 4,  // 1
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  // 2
    3, 3, 4, 4, 3, 3, 3, 3, 5, 5, 3, 10, 4, 10, 9, 12,  // 3
    2, 4, 4, 2, 2, 4, 2, 2, 2, 2, 2, 4, 2, 2, 4, 2,  // 4
    2, 4, 4, 2, 2, 4, 2, 2, 2, 2, 2, 4, 2, 2, 4, 2,  // 5
    6, 4, 4, 6, 6, 4, 6, 6, 6, 6, 6, 4, 6, 6, 3, 6,  // 6
    6, 4, 4, 6, 6, 4, 6, 6, 6, 6, 6, 4, 6, 6, 3, 6,  // 7
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 4, 6, 3, 3,  // 8
    3, 3, 3, 5, 3, 3, 3, 3, 3, 3, 3, 3, 5, 5, 4, 4,  // 9
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 5, 5,  // A
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 5, 5,  // B
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 3, 3,  // C
    3, 3, 3, 5, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,  // D
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,  // E
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,  // F
};
inline constexpr u8 kCycles6301[256] = {
    12, 1, 12, 12, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0
    1, 1, 4, 4, 12, 12, 1, 1, 2, 2, 4, 1, 12, 12, 12, 12,  // 1
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  // 2
    1, 1, 3, 3, 1, 1, 4, 4, 4, 5, 1, 10, 5, 7, 9, 12,  // 3
    1, 12, 12, 1, 1, 12, 1, 1, 1, 1, 1, 12, 1, 1, 12, 1,  // 4
    1, 12, 12, 1, 1, 12, 1, 1, 1, 1, 1, 12, 1, 1, 12, 1,  // 5
    6, 7, 7, 6, 6, 7, 6, 6, 6, 6, 6, 5, 6, 4, 3, 5,  // 6
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 4, 6, 4, 3, 5,  // 7
    2, 2, 2, 3, 2, 2, 2, 12, 2, 2, 2, 2, 3, 5, 3, 12,  // 8
    3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 5, 4, 4,  // 9
    4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,  // A
    4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6, 5, 5,  // B
    2, 2, 2, 3, 2, 2, 2, 12, 2, 2, 2, 2, 3, 12, 3, 12,  // C
    3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,  // D
    4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,  // E
    4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,  // F
};

// HC11 page 0. The MC6801's except RTI / SWI / WAI and the new opcodes; a
// prebyte adds one. The opcodes that trap are listed with the trap's length.
inline constexpr u8 kCyclesHc11[256] = {
    14, 2, 41, 41, 3, 3, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2,  // 0
    2, 2, 6, 6, 6, 6, 2, 2, 0, 2, 0, 2, 7, 7, 7, 7,  // 1
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  // 2
    3, 3, 4, 4, 3, 3, 3, 3, 5, 5, 3, 12, 4, 10, 14, 14,  // 3
    2, 14, 14, 2, 2, 14, 2, 2, 2, 2, 2, 14, 2, 2, 14, 2,  // 4
    2, 14, 14, 2, 2, 14, 2, 2, 2, 2, 2, 14, 2, 2, 14, 2,  // 5
    6, 14, 14, 6, 6, 14, 6, 6, 6, 6, 6, 14, 6, 6, 3, 6,  // 6
    6, 14, 14, 6, 6, 14, 6, 6, 6, 6, 6, 14, 6, 6, 3, 6,  // 7
    2, 2, 2, 4, 2, 2, 2, 14, 2, 2, 2, 2, 4, 6, 3, 3,  // 8
    3, 3, 3, 5, 3, 3, 3, 3, 3, 3, 3, 3, 5, 5, 4, 4,  // 9
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 5, 5,  // A
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 5, 5,  // B
    2, 2, 2, 4, 2, 2, 2, 14, 2, 2, 2, 2, 3, 0, 3, 2,  // C
    3, 3, 3, 5, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,  // D
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,  // E
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,  // F
};

// Interrupt sequence (and TRAP): the cycle of the first of the stack pushes
// (seven, nine on the HC11) and of the vector fetch, and its length.
struct EntryTiming {
  u8 push, vector, cycles;
};
constexpr EntryTiming entry_timing(Family f) {
  return f == Family::HD6301 ? EntryTiming{2, 9, 12} : f == Family::HC11 ? EntryTiming{2, 12, 14} : EntryTiming{2, 10, 12};
}
// Leaving WAI: the vector fetch only, the state being stacked already.
constexpr u8 kWaiExitCycles = 4;

namespace detail {

// Where the data accesses of an instruction fall (0-based cycle).
enum class Access : u8 { None, Read, Write, Rmw, Tst, Clr, BitOp, Tim, Jsr, Push, Pull, Swi, Bset, Brset };

// `pre`: the HC11 prebyte's cycle, which delays every access by one.
inline void access_cycles(DecodedInsn& d, Family f, Access k, unsigned pre = 0) {
  const bool hd = f == Family::HD6301;
  const Mode m = d.mode;
  // The operand cycle of the plain read / write forms.
  const u8 operand = hd ? (m == Mode::Dir ? 1 : 2) : (m == Mode::Dir ? 2 : 3);
  switch (k) {
    case Access::None: break;
    case Access::Read: case Access::Write: case Access::Tst: d.d0 = operand; break;
    case Access::Rmw: d.d0 = operand; d.d1 = u8(operand + 2); break;
    case Access::Clr: d.d0 = operand; d.d1 = u8(operand + (hd ? 1 : 2)); break;
    case Access::BitOp: d.d0 = m == Mode::Dir ? 2 : 3; d.d1 = u8(d.d0 + 2); break;
    case Access::Tim: d.d0 = m == Mode::Dir ? 2 : 3; break;
    case Access::Jsr:
      if (hd) d.d0 = m == Mode::Ext ? 3 : 2;
      else d.d0 = m == Mode::Dir ? 3 : 4;
      break;
    case Access::Push: d.d0 = 2; break;
    case Access::Pull: d.d0 = hd ? 2 : 3; break;  // the MC6801 reads the stack top first
    case Access::Swi: d.d0 = 2; d.d1 = entry_timing(f).vector; break;
    // HC11: the operand read, the mask fetch, an internal cycle, the store.
    case Access::Bset: d.d0 = m == Mode::Dir ? 2 : 3; d.d1 = u8(d.d0 + 3); break;
    case Access::Brset: d.d0 = m == Mode::Dir ? 2 : 3; break;
  }
  if (pre && k != Access::None) {
    d.d0 = u8(d.d0 + pre);
    if (d.d1) d.d1 = u8(d.d1 + pre);
  }
}

// Whether a prebyte page holds the opcode.
constexpr bool hc11_on_page(u8 page, u8 op) {
  const unsigned hi = op >> 4, lo = op & 15;
  switch (page) {
    case 0x18:
      switch (hi) {
        case 0x0: return lo == 0x8 || lo == 0x9;
        case 0x1: return lo >= 0xC;
        case 0x3: return lo == 0x0 || lo == 0x5 || lo == 0x8 || lo == 0xA || lo == 0xC;
        case 0x6: return lo != 0x1 && lo != 0x2 && lo != 0x5 && lo != 0xB;
        case 0x8: return lo == 0xC || lo == 0xF;
        case 0x9: case 0xB: return lo == 0xC;
        case 0xA: case 0xE: return true;
        case 0xC: return lo == 0xE;
        case 0xD: case 0xF: return lo == 0xE || lo == 0xF;
        default: return false;
      }
    case 0x1A: return op == 0x83 || op == 0x93 || op == 0xA3 || op == 0xB3 || op == 0xAC || op == 0xEE || op == 0xEF;
    case 0xCD: return op == 0xA3 || op == 0xAC || op == 0xEE || op == 0xEF;
    default: return true;
  }
}

template <class Fetch>
DecodedInsn decode_hc11(u16 pc, Fetch fetch) {
  DecodedInsn d;
  u8 op = fetch(0);
  unsigned pre = 0;
  if (op == 0x18 || op == 0x1A || op == 0xCD) {
    d.page = op;
    pre = 1;
    op = fetch(1);
  }
  d.opcode = op;
  d.cyc = u8(kCyclesHc11[op] + pre);
  auto b1 = [&] { return u16(fetch(pre + 1)); };
  auto w1 = [&] { return u16((fetch(pre + 1) << 8) | fetch(pre + 2)); };
  auto undefined = [&] {
    // The trap stacks the address of the opcode (or of its prebyte).
    d.op = Op::Trap;
    d.mode = Mode::Inh;
    d.len = 1;
    d.cyc = entry_timing(Family::HC11).cycles;
    return d;
  };
  if (!hc11_on_page(d.page, op)) return undefined();
  // Indexed addressing: by Y on pages $18 and $CD. The index register of
  // the X instructions: Y on pages $18 and $1A.
  const Mode idx = d.page == 0x18 || d.page == 0xCD ? Mode::Idy : Mode::Idx;
  const u8 ir = d.page == 0x18 || d.page == 0x1A ? 1 : 0;
  auto set = [&](Op o, Mode m, Access k) {
    d.op = o;
    d.mode = m;
    switch (m) {
      case Mode::Inh: d.len = u8(pre + 1); break;
      case Mode::Dir: case Mode::Idx: case Mode::Idy: d.imm = b1(); d.len = u8(pre + 2); break;
      case Mode::Ext: d.imm = w1(); d.len = u8(pre + 3); break;
      case Mode::Rel: d.imm = u16(pc + pre + 2 + s8(fetch(pre + 1))); d.len = u8(pre + 2); break;
      case Mode::Imm: break;  // sized by the caller
    }
    access_cycles(d, Family::HC11, k, pre);
    return d;
  };
  auto xop = [&](Op o, Mode m, Access k) {
    d.reg = ir;
    return set(o, m, k);
  };
  // BSET / BCLR: operand, mask. BRSET / BRCLR: operand, mask, displacement.
  auto bitop = [&](Op o, Mode m) {
    d.op = o;
    d.mode = m;
    d.x = fetch(pre + 2);
    if (o == Op::Bset || o == Op::Bclr) {
      d.imm = b1();
      d.len = u8(pre + 3);
      access_cycles(d, Family::HC11, Access::Bset, pre);
    } else {
      d.ea = u8(b1());
      d.len = u8(pre + 4);
      d.imm = u16(pc + d.len + s8(fetch(pre + 3)));
      access_cycles(d, Family::HC11, Access::Brset, pre);
    }
    return d;
  };

  const unsigned hi = op >> 4, lo = op & 15;
  switch (hi) {
    case 0x0: {
      static constexpr Op ops[16] = {Op::Illegal, Op::Nop, Op::Idiv, Op::Fdiv, Op::Lsrd, Op::Asld, Op::Tap, Op::Tpa,
                                     Op::Inx, Op::Dex, Op::Clv, Op::Sev, Op::Clc, Op::Sec, Op::Cli, Op::Sei};
      if (ops[lo] == Op::Illegal) return undefined();
      if (ops[lo] == Op::Inx || ops[lo] == Op::Dex) return xop(ops[lo], Mode::Inh, Access::None);
      return set(ops[lo], Mode::Inh, Access::None);
    }
    case 0x1:
      switch (lo) {
        case 0x0: return set(Op::Sba, Mode::Inh, Access::None);
        case 0x1: return set(Op::Cba, Mode::Inh, Access::None);
        case 0x2: return bitop(Op::Brset, Mode::Dir);
        case 0x3: return bitop(Op::Brclr, Mode::Dir);
        case 0x4: return bitop(Op::Bset, Mode::Dir);
        case 0x5: return bitop(Op::Bclr, Mode::Dir);
        case 0x6: return set(Op::Tab, Mode::Inh, Access::None);
        case 0x7: return set(Op::Tba, Mode::Inh, Access::None);
        case 0x9: return set(Op::Daa, Mode::Inh, Access::None);
        case 0xB: return set(Op::Aba, Mode::Inh, Access::None);
        case 0xC: return bitop(Op::Bset, idx);
        case 0xD: return bitop(Op::Bclr, idx);
        case 0xE: return bitop(Op::Brset, idx);
        case 0xF: return bitop(Op::Brclr, idx);
        default: return undefined();  // $18 / $1A as the second byte of a page
      }
    case 0x2: d.cond = u8(lo); return set(Op::Bcc, Mode::Rel, Access::None);
    case 0x3:
      switch (lo) {
        case 0x0: return xop(Op::Tsx, Mode::Inh, Access::None);
        case 0x1: return set(Op::Ins, Mode::Inh, Access::None);
        case 0x2: case 0x3: d.reg = u8(lo & 1); return set(Op::Pul, Mode::Inh, Access::Pull);
        case 0x4: return set(Op::Des, Mode::Inh, Access::None);
        case 0x5: return xop(Op::Txs, Mode::Inh, Access::None);
        case 0x6: case 0x7: d.reg = u8(lo & 1); return set(Op::Psh, Mode::Inh, Access::Push);
        case 0x8: return xop(Op::Pulx, Mode::Inh, Access::Pull);
        case 0x9: return set(Op::Rts, Mode::Inh, Access::Pull);
        case 0xA: return xop(Op::Abx, Mode::Inh, Access::None);
        case 0xB: return set(Op::Rti, Mode::Inh, Access::Pull);
        case 0xC: return xop(Op::Pshx, Mode::Inh, Access::Push);
        case 0xD: return set(Op::Mul, Mode::Inh, Access::None);
        case 0xE: return set(Op::Wai, Mode::Inh, Access::Push);
        default: return set(Op::Swi, Mode::Inh, Access::Swi);
      }
    case 0x4: case 0x5: case 0x6: case 0x7: {
      static constexpr Op ops[16] = {Op::Neg, Op::Illegal, Op::Illegal, Op::Com, Op::Lsr, Op::Illegal, Op::Ror, Op::Asr,
                                     Op::Asl, Op::Rol, Op::Dec, Op::Illegal, Op::Inc, Op::Tst, Op::Jmp, Op::Clr};
      const Op o = ops[lo];
      if (o == Op::Illegal) return undefined();
      if (hi < 6) {
        if (o == Op::Jmp) return undefined();
        d.reg = u8(hi & 1);
        return set(o, Mode::Inh, Access::None);
      }
      const Mode m = hi == 6 ? idx : Mode::Ext;
      if (o == Op::Jmp) return set(o, m, Access::None);
      return set(o, m, o == Op::Tst ? Access::Tst : o == Op::Clr ? Access::Clr : Access::Rmw);
    }
    default: break;
  }

  // $80-$FF: the accumulator groups. Bits 5-4 select the addressing mode.
  const Mode m = (hi & 3) == 0 ? Mode::Imm : (hi & 3) == 1 ? Mode::Dir : (hi & 3) == 2 ? idx : Mode::Ext;
  const bool bside = hi >= 0xC;
  auto operand = [&](Op o, Access k) {
    if (m != Mode::Imm) return set(o, m, k);
    d.imm = b1();
    d.len = u8(pre + 2);
    return set(o, Mode::Imm, Access::None);
  };
  auto operand16 = [&](Op o) {
    if (m != Mode::Imm) return set(o, m, Access::Read);
    d.imm = w1();
    d.len = u8(pre + 3);
    return set(o, Mode::Imm, Access::None);
  };
  auto xoperand16 = [&](Op o) {
    d.reg = ir;
    return operand16(o);
  };
  d.reg = bside ? 1 : 0;
  switch (lo) {
    case 0x0: return operand(Op::Sub, Access::Read);
    case 0x1: return operand(Op::Cmp, Access::Read);
    case 0x2: return operand(Op::Sbc, Access::Read);
    case 0x3: return operand16(bside ? Op::Addd : d.page == 0x1A || d.page == 0xCD ? Op::Cpd : Op::Subd);
    case 0x4: return operand(Op::And, Access::Read);
    case 0x5: return operand(Op::Bit, Access::Read);
    case 0x6: return operand(Op::Lda, Access::Read);
    case 0x7: if (m == Mode::Imm) return undefined(); return set(Op::Sta, m, Access::Write);
    case 0x8: return operand(Op::Eor, Access::Read);
    case 0x9: return operand(Op::Adc, Access::Read);
    case 0xA: return operand(Op::Ora, Access::Read);
    case 0xB: return operand(Op::Add, Access::Read);
    case 0xC: return bside ? operand16(Op::Ldd) : xoperand16(Op::Cpx);
    case 0xD:
      if (bside) {
        if (m == Mode::Imm) return undefined();  // $CD is a prebyte
        return set(Op::Std, m, Access::Write);
      }
      if (m == Mode::Imm) {
        set(Op::Bsr, Mode::Rel, Access::None);
        d.d0 = 4;  // the return address pushes, as JSR indexed
        return d;
      }
      return set(Op::Jsr, m, Access::Jsr);
    case 0xE: return bside ? xoperand16(Op::Ldx) : operand16(Op::Lds);
    default:
      if (m == Mode::Imm) {
        if (bside) return set(Op::Stop, Mode::Inh, Access::None);
        return xop(Op::Xgdx, Mode::Inh, Access::None);
      }
      if (bside) return xop(Op::Stx, m, Access::Write);
      return set(Op::Sts, m, Access::Write);
  }
}

}  // namespace detail

// `fetch(i)` returns the code byte at pc + i.
template <class Fetch>
DecodedInsn decode(Family f, u16 pc, Fetch fetch) {
  using detail::Access;
  if (f == Family::HC11) return detail::decode_hc11(pc, fetch);
  const bool hd = f == Family::HD6301;
  DecodedInsn d;
  const u8 op = fetch(0);
  d.opcode = op;
  d.cyc = hd ? kCycles6301[op] : kCycles6801[op];
  auto b1 = [&] { return u16(fetch(1)); };
  auto w1 = [&] { return u16((fetch(1) << 8) | fetch(2)); };
  auto undefined = [&] {
    d.op = hd ? Op::Trap : Op::Illegal;
    d.mode = Mode::Inh;
    d.len = 1;
    return d;
  };
  auto set = [&](Op o, Mode m, Access k) {
    d.op = o;
    d.mode = m;
    switch (m) {
      case Mode::Inh: d.len = 1; break;
      case Mode::Dir: case Mode::Idx: case Mode::Idy: d.imm = b1(); d.len = 2; break;
      case Mode::Ext: d.imm = w1(); d.len = 3; break;
      case Mode::Rel: d.imm = u16(pc + 2 + s8(fetch(1))); d.len = 2; break;
      case Mode::Imm: break;  // sized by the caller
    }
    detail::access_cycles(d, f, k);
    return d;
  };
  auto imm8 = [&](Op o) { d.imm = b1(); d.len = 2; return set(o, Mode::Imm, Access::None); };
  auto imm16 = [&](Op o) { d.imm = w1(); d.len = 3; return set(o, Mode::Imm, Access::None); };

  const unsigned hi = op >> 4, lo = op & 15;
  switch (hi) {
    case 0x0: {
      static constexpr Op ops[16] = {Op::Illegal, Op::Nop, Op::Illegal, Op::Illegal, Op::Lsrd, Op::Asld, Op::Tap,
                                     Op::Tpa, Op::Inx, Op::Dex, Op::Clv, Op::Sev, Op::Clc, Op::Sec, Op::Cli, Op::Sei};
      if (ops[lo] == Op::Illegal) return undefined();
      return set(ops[lo], Mode::Inh, Access::None);
    }
    case 0x1:
      switch (lo) {
        case 0x0: return set(Op::Sba, Mode::Inh, Access::None);
        case 0x1: return set(Op::Cba, Mode::Inh, Access::None);
        case 0x2: case 0x3: if (!hd) return undefined(); return set(Op::Undoc, Mode::Inh, Access::Pull);
        case 0x6: return set(Op::Tab, Mode::Inh, Access::None);
        case 0x7: return set(Op::Tba, Mode::Inh, Access::None);
        case 0x8: if (!hd) return undefined(); return set(Op::Xgdx, Mode::Inh, Access::None);
        case 0x9: return set(Op::Daa, Mode::Inh, Access::None);
        case 0xA: if (!hd) return undefined(); return set(Op::Slp, Mode::Inh, Access::None);
        case 0xB: return set(Op::Aba, Mode::Inh, Access::None);
        default: return undefined();
      }
    case 0x2: d.cond = u8(lo); return set(Op::Bcc, Mode::Rel, Access::None);
    case 0x3:
      switch (lo) {
        case 0x0: return set(Op::Tsx, Mode::Inh, Access::None);
        case 0x1: return set(Op::Ins, Mode::Inh, Access::None);
        case 0x2: case 0x3: d.reg = u8(lo & 1); return set(Op::Pul, Mode::Inh, Access::Pull);
        case 0x4: return set(Op::Des, Mode::Inh, Access::None);
        case 0x5: return set(Op::Txs, Mode::Inh, Access::None);
        case 0x6: case 0x7: d.reg = u8(lo & 1); return set(Op::Psh, Mode::Inh, Access::Push);
        case 0x8: return set(Op::Pulx, Mode::Inh, Access::Pull);
        case 0x9: return set(Op::Rts, Mode::Inh, Access::Pull);
        case 0xA: return set(Op::Abx, Mode::Inh, Access::None);
        case 0xB: return set(Op::Rti, Mode::Inh, Access::Pull);
        case 0xC: return set(Op::Pshx, Mode::Inh, Access::Push);
        case 0xD: return set(Op::Mul, Mode::Inh, Access::None);
        case 0xE: return set(Op::Wai, Mode::Inh, Access::Push);
        default: return set(Op::Swi, Mode::Inh, Access::Swi);
      }
    case 0x4: case 0x5: case 0x6: case 0x7: {
      static constexpr Op ops[16] = {Op::Neg, Op::Aim, Op::Oim, Op::Com, Op::Lsr, Op::Eim, Op::Ror, Op::Asr,
                                     Op::Asl, Op::Rol, Op::Dec, Op::Tim, Op::Inc, Op::Tst, Op::Jmp, Op::Clr};
      const Op o = ops[lo];
      if (hi < 6) {
        if (o == Op::Aim || o == Op::Oim || o == Op::Eim || o == Op::Tim || o == Op::Jmp) return undefined();
        d.reg = u8(hi & 1);
        return set(o, Mode::Inh, Access::None);
      }
      const Mode m = hi == 6 ? Mode::Idx : Mode::Ext;
      if (o == Op::Aim || o == Op::Oim || o == Op::Eim || o == Op::Tim) {
        // HD6301: opcode, immediate, then the index offset ($6x) or direct address ($7x).
        if (!hd) return undefined();
        d.op = o;
        d.mode = hi == 6 ? Mode::Idx : Mode::Dir;
        d.x = fetch(1);
        d.imm = fetch(2);
        d.len = 3;
        detail::access_cycles(d, f, o == Op::Tim ? Access::Tim : Access::BitOp);
        return d;
      }
      if (o == Op::Jmp) return set(o, m, Access::None);
      return set(o, m, o == Op::Tst ? Access::Tst : o == Op::Clr ? Access::Clr : Access::Rmw);
    }
    default: break;
  }

  // $80-$FF: the accumulator groups. Bits 5-4 select the addressing mode.
  const Mode m = (hi & 3) == 0 ? Mode::Imm : (hi & 3) == 1 ? Mode::Dir : (hi & 3) == 2 ? Mode::Idx : Mode::Ext;
  const bool bside = hi >= 0xC;
  auto operand = [&](Op o, Access k) { return m == Mode::Imm ? imm8(o) : set(o, m, k); };
  auto operand16 = [&](Op o) { return m == Mode::Imm ? imm16(o) : set(o, m, Access::Read); };
  // MC6801 store-immediate: the register goes over the instruction's own operand bytes.
  auto store_imm = [&](Op o, u8 bytes) {
    if (hd) return undefined();
    d.op = o;
    d.mode = Mode::Ext;
    d.imm = u16(pc + 1);
    d.len = u8(1 + bytes);
    detail::access_cycles(d, f, Access::Write);
    return d;
  };
  d.reg = bside ? 1 : 0;
  switch (lo) {
    case 0x0: return operand(Op::Sub, Access::Read);
    case 0x1: return operand(Op::Cmp, Access::Read);
    case 0x2: return operand(Op::Sbc, Access::Read);
    case 0x3: return operand16(bside ? Op::Addd : Op::Subd);
    case 0x4: return operand(Op::And, Access::Read);
    case 0x5: return operand(Op::Bit, Access::Read);
    case 0x6: return operand(Op::Lda, Access::Read);
    case 0x7: if (m == Mode::Imm) return store_imm(Op::Sta, 1); return set(Op::Sta, m, Access::Write);
    case 0x8: return operand(Op::Eor, Access::Read);
    case 0x9: return operand(Op::Adc, Access::Read);
    case 0xA: return operand(Op::Ora, Access::Read);
    case 0xB: return operand(Op::Add, Access::Read);
    case 0xC: return operand16(bside ? Op::Ldd : Op::Cpx);
    case 0xD:
      if (bside) {
        if (m == Mode::Imm) return store_imm(Op::Std, 2);
        return set(Op::Std, m, Access::Write);
      }
      if (m == Mode::Imm) {
        set(Op::Bsr, Mode::Rel, Access::None);
        d.d0 = hd ? 2 : 4;  // the return address pushes, as JSR indexed
        return d;
      }
      return set(Op::Jsr, m, Access::Jsr);
    case 0xE: d.reg = 0; return operand16(bside ? Op::Ldx : Op::Lds);  // reg: the index register, X
    default:
      d.reg = 0;
      if (m == Mode::Imm) return store_imm(bside ? Op::Stx : Op::Sts, 2);
      return set(bside ? Op::Stx : Op::Sts, m, Access::Write);
  }
}

}  // namespace m6801

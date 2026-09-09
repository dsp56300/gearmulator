// Instruction handlers ("threaded code") and instruction-cache fill.
//
// Every handler has the signature `const Cell* (Cpu&, const Cell*)`, performs
// one instruction, charges its states to the budget and tail-calls the next
// cell's handler.  Handlers are templates over operation, operand size and
// addressing mode so that operand access is fully resolved at compile time and
// no per-instruction decoding or mode dispatch remains at run time.
#include <bit>

#include "cpu/h8500/cpu.hpp"

namespace h8500 {

#if defined(__clang__) && __has_cpp_attribute(clang::musttail)
#define H8_HAS_MUSTTAIL 1
#else
#define H8_HAS_MUSTTAIL 0
#endif

#if H8_HAS_MUSTTAIL
// Charge `states`, then continue with `next` unless the budget ran out (or a
// pending condition forced it negative), in which case return the resume point.
#define H8_END(c, next, states)                          \
  (c).budget_ -= s32(states);                            \
  if (H8_UNLIKELY((c).budget_ <= 0)) return (next);     \
  [[clang::musttail]] return (next)->fn((c), (next))
// Re-dispatch the same cell (after filling it).
#define H8_GOTO(c, ip) [[clang::musttail]] return (ip)->fn((c), (ip))
#else
// Without guaranteed tail calls the run loop dispatches; handlers just return
// the next cell.
#define H8_END(c, next, states) \
  (c).budget_ -= s32(states);   \
  return (next)
#define H8_GOTO(c, ip) return (ip)
#endif

struct ExecImpl {
  static constexpr u16 kC = Cpu::kC, kV = Cpu::kV, kZ = Cpu::kZ, kN = Cpu::kN;

  // Two-operand "source EA -> register" operations.
  enum class Alu : u8 { Add, Sub, And, Or, Xor, Addx, Subx, Cmp, Mov, MovFpe, Adds, Subs, Ldc, Mulxu, Divxu };
  // Operations on the EA operand itself (read-modify-write, or write-only).
  enum class Rmw : u8 {
    AddQ, Neg, Not, Clr, Tst, Tas,
    Shal, Shar, Shll, Shlr, Rotl, Rotr, Rotxl, Rotxr,
    BsetI, BclrI, BnotI, BtstI, BsetR, BclrR, BnotR, BtstR,
    CmpImm, MovImm, MovStore, MovTpe, Stc
  };

  // ---------------------------------------------------------------------------
  // Cell / program-counter helpers

  static u16 pc_of(const Cpu& c, const Cell* ip) { return u16(ip - c.page_cells_); }
  static unsigned len_of(const Cell* ip) { return ip->x & 7; }
  static const Cell* seq(const Cell* ip) { return ip + len_of(ip); }
  static const Cell* here(const Cpu& c, u16 pc) { return c.page_cells_ + pc; }
  static u16 pc_next(const Cpu& c, const Cell* ip) { return u16(pc_of(c, ip) + len_of(ip)); }
  // Second 16-bit parameter (OP-side immediate of MOV/CMP/ADD:Q #xx,<EAd>),
  // kept in the unused alternate-timing fields.
  static u16 op_imm(const Cell* ip) { return u16((u16(ip->cyc2) << 8) | ip->icnt2); }

  // ---------------------------------------------------------------------------
  // Register / memory access

  template <Size SZ>
  static u32 get_reg(const Cpu& c, unsigned r) {
    if constexpr (SZ == Size::Word) return c.regs_.r[r];
    else return c.regs_.r[r] & 0xFF;
  }
  template <Size SZ>
  static void set_reg(Cpu& c, unsigned r, u32 v) {
    if constexpr (SZ == Size::Word) c.regs_.r[r] = u16(v);
    else c.regs_.r[r] = u16((c.regs_.r[r] & 0xFF00) | (v & 0xFF));
  }
  template <Size SZ>
  static u32 mrd(Cpu& c, u32 a) {
    if constexpr (SZ == Size::Word) return c.mem_read16(a);
    else return c.mem_read8(a);
  }
  template <Size SZ>
  static void mwr(Cpu& c, u32 a, u32 v) {
    if constexpr (SZ == Size::Word) c.mem_write16(a, v);
    else c.mem_write8(a, v);
  }

  // Extra states for `icnt` operand bytes accessed in a region with attribute
  // byte `attr` (2.6.1 / 2.6.4 correction terms, plus wait states).
  template <Size SZ>
  static unsigned opnd_pen(u8 attr, unsigned icnt) {
    const unsigned cls = attr & Bus::kClassMask;
    // Penalty per I in half-units, indexed by BusClass: W16_S2, W16_S3, W8_S2, W8_S3.
    constexpr u8 kByte[4] = {0, 2, 0, 2};  // 0, I,   0, I
    constexpr u8 kWord[4] = {0, 1, 2, 4};  // 0, I/2, I, 2I
    unsigned pen = (((SZ == Size::Word) ? kWord[cls] : kByte[cls]) * icnt) >> 1;
    const unsigned wait = attr >> Bus::kWaitShift;
    if (H8_UNLIKELY(wait)) pen += wait * operand_bus_cycles(BusClass(cls), icnt, SZ);
    return pen;
  }
  static unsigned stack_pen(const Cpu& c, unsigned icnt) { return opnd_pen<Size::Word>(c.last_attr_, icnt); }
  template <EaMode M, Size SZ>
  static unsigned ea_pen(const Cpu& c, const Cell* ip) {
    if constexpr (ea_is_memory(M)) return opnd_pen<SZ>(c.last_attr_, ip->icnt);
    else return 0;
  }

  // ---------------------------------------------------------------------------
  // Condition codes (Table 2-7)

  template <Size SZ>
  static void set_nz(Cpu& c, u32 v) {
    u16 f = u16(c.regs_.sr & ~(kN | kZ));
    v &= size_mask(SZ);
    if (v & size_msb(SZ)) f |= kN;
    if (v == 0) f |= kZ;
    c.regs_.sr = f;
  }
  // r = a + b (+ C); a, b masked to the operand size, r the raw wider sum.
  template <Size SZ>
  static void flags_add(Cpu& c, u32 a, u32 b, u32 r, bool extend) {
    constexpr u32 m = size_mask(SZ), msb = size_msb(SZ);
    u16 f = u16(c.regs_.sr & ~(kN | kZ | kV | kC));
    if (r & msb) f |= kN;
    if ((r & m) == 0 && (!extend || (c.regs_.sr & kZ))) f |= kZ;
    if (~(a ^ b) & (a ^ r) & msb) f |= kV;
    if (r & (msb << 1)) f |= kC;
    c.regs_.sr = f;
  }
  // r = a - b (- C) computed in u32; borrow appears in bit 8 / bit 16.
  template <Size SZ>
  static void flags_sub(Cpu& c, u32 a, u32 b, u32 r, bool extend) {
    constexpr u32 m = size_mask(SZ), msb = size_msb(SZ);
    u16 f = u16(c.regs_.sr & ~(kN | kZ | kV | kC));
    if (r & msb) f |= kN;
    if ((r & m) == 0 && (!extend || (c.regs_.sr & kZ))) f |= kZ;
    if ((a ^ b) & (a ^ r) & msb) f |= kV;
    if (r & (msb << 1)) f |= kC;
    c.regs_.sr = f;
  }
  // Condition test compiled per condition code (the Bcc handler is
  // instantiated per cc, so no per-execution dispatch on the condition).
  template <Cc CC>
  static bool cond_true(u16 sr) {
    const bool c = sr & kC, v = sr & kV, z = sr & kZ, n = sr & kN;
    switch (CC) {
      case Cc::T: return true;
      case Cc::F: return false;
      case Cc::HI: return !(c || z);
      case Cc::LS: return c || z;
      case Cc::CC: return !c;
      case Cc::CS: return c;
      case Cc::NE: return !z;
      case Cc::EQ: return z;
      case Cc::VC: return !v;
      case Cc::VS: return v;
      case Cc::PL: return !n;
      case Cc::MI: return n;
      case Cc::GE: return n == v;
      case Cc::LT: return n != v;
      case Cc::GT: return !z && (n == v);
      case Cc::LE: return z || (n != v);
    }
    return false;
  }

  // BCD add/sub of two packed bytes with carry/borrow in.
  static u8 bcd_add(u8 a, u8 b, bool cin, bool& carry_out) {
    unsigned lo = (a & 0x0F) + (b & 0x0F) + (cin ? 1 : 0);
    unsigned hi = (a >> 4) + (b >> 4);
    if (lo > 9) { lo -= 10; hi += 1; }
    carry_out = false;
    if (hi > 9) { hi -= 10; carry_out = true; }
    return u8((hi << 4) | lo);
  }
  static u8 bcd_sub(u8 a, u8 b, bool cin, bool& borrow_out) {
    int lo = int(a & 0x0F) - int(b & 0x0F) - (cin ? 1 : 0);
    int hi = int(a >> 4) - int(b >> 4);
    if (lo < 0) { lo += 10; hi -= 1; }
    borrow_out = false;
    if (hi < 0) { hi += 10; borrow_out = true; }
    return u8((hi << 4) | lo);
  }

  // ---------------------------------------------------------------------------
  // Effective address (Table 1-10).  The EA register is in the high nibble of
  // Cell::r, the EA extension (displacement / address) in Cell::imm.

  template <EaMode M, Size SZ>
  static u32 ea(Cpu& c, const Cell* ip) {
    const unsigned n = ip->r >> 4;
    Cpu::Regs& R = c.regs_;
    if constexpr (M == EaMode::RegInd) {
      return c.data_addr(n, R.r[n]);
    } else if constexpr (M == EaMode::Disp8 || M == EaMode::Disp16) {
      return c.data_addr(n, u16(R.r[n] + s16(ip->imm)));
    } else if constexpr (M == EaMode::PreDec) {
      // Decrement by 1 (byte) or 2 (word); always 2 for R7 (SP), and a byte
      // pushed through @-SP is a word access (manual 4.10) that leaves the
      // byte in the odd (low) lane of the word slot.
      if constexpr (SZ == Size::Byte) {
        if (n == 7) { R.r[7] = u16(R.r[7] - 2); return c.data_addr(7, u16(R.r[7] + 1)); }
        R.r[n] = u16(R.r[n] - 1);
        return c.data_addr(n, R.r[n]);
      } else {
        R.r[n] = u16(R.r[n] - 2);
        return c.data_addr(n, R.r[n]);
      }
    } else if constexpr (M == EaMode::PostInc) {
      // Increment during the EA phase so that a register that is both pointer
      // and destination ends up holding the loaded value (MOV.W @R0+,R0).
      if constexpr (SZ == Size::Byte) {
        if (n == 7) { const u32 a = c.data_addr(7, u16(R.r[7] + 1)); R.r[7] = u16(R.r[7] + 2); return a; }
        const u32 a = c.data_addr(n, R.r[n]);
        R.r[n] = u16(R.r[n] + 1);
        return a;
      } else {
        const u32 a = c.data_addr(n, R.r[n]);
        R.r[n] = u16(R.r[n] + 2);
        return a;
      }
    } else if constexpr (M == EaMode::Abs8) {
      return c.abs8_addr(u8(ip->imm));
    } else {
      static_assert(M == EaMode::Abs16);
      return c.abs16_addr(ip->imm);
    }
  }

  // Destination operand: register (EA register) or memory at the EA.
  template <EaMode M, Size SZ>
  struct Opnd {
    Cpu& c;
    u32 a;
    Opnd(Cpu& cpu, const Cell* ip) : c(cpu), a(0) {
      if constexpr (M == EaMode::Reg) a = ip->r >> 4;
      else a = ea<M, SZ>(cpu, ip);
    }
    u32 read() const {
      if constexpr (M == EaMode::Reg) return get_reg<SZ>(c, a);
      else return mrd<SZ>(c, a);
    }
    void write(u32 v) const {
      if constexpr (M == EaMode::Reg) set_reg<SZ>(c, a, v);
      else mwr<SZ>(c, a, v);
    }
  };

  // ---------------------------------------------------------------------------
  // <EAs>, Rd operations

  template <Alu K, Size SZ, EaMode M>
  static const Cell* h_alu(Cpu& c, const Cell* ip) {
    constexpr u32 m = size_mask(SZ);
    Cpu::Regs& R = c.regs_;
    const unsigned rd = ip->r & 7;
    u32 s;
    if constexpr (M == EaMode::Reg) s = get_reg<SZ>(c, ip->r >> 4);
    else if constexpr (M == EaMode::Imm) s = u32(ip->imm) & m;
    else s = mrd<SZ>(c, ea<M, SZ>(c, ip));
    ++c.insn_count_;
    const Cell* next = seq(ip);
    unsigned states = ip->cyc + ea_pen<M, SZ>(c, ip);

    if constexpr (K == Alu::Add) {
      const u32 a = get_reg<SZ>(c, rd), r = a + s;
      flags_add<SZ>(c, a, s, r, false);
      set_reg<SZ>(c, rd, r);
    } else if constexpr (K == Alu::Sub) {
      const u32 a = get_reg<SZ>(c, rd), r = a - s;
      flags_sub<SZ>(c, a, s, r, false);
      set_reg<SZ>(c, rd, r);
    } else if constexpr (K == Alu::Addx) {
      const u32 a = get_reg<SZ>(c, rd), r = a + s + ((R.sr & kC) ? 1u : 0u);
      flags_add<SZ>(c, a, s, r, true);
      set_reg<SZ>(c, rd, r);
    } else if constexpr (K == Alu::Subx) {
      // Unlike ADDX, SUBX's Z is not sticky (manual 2.2.54: "set when the result is zero").
      const u32 a = get_reg<SZ>(c, rd), r = a - s - ((R.sr & kC) ? 1u : 0u);
      flags_sub<SZ>(c, a, s, r, false);
      set_reg<SZ>(c, rd, r);
    } else if constexpr (K == Alu::Cmp) {
      const u32 a = get_reg<SZ>(c, rd);
      flags_sub<SZ>(c, a, s, a - s, false);
    } else if constexpr (K == Alu::And || K == Alu::Or || K == Alu::Xor) {
      const u32 a = get_reg<SZ>(c, rd);
      const u32 r = (K == Alu::And) ? (a & s) : (K == Alu::Or) ? (a | s) : (a ^ s);
      set_reg<SZ>(c, rd, r);
      set_nz<SZ>(c, r);
      R.sr &= u16(~kV);
    } else if constexpr (K == Alu::Mov) {
      set_reg<SZ>(c, rd, s);
      set_nz<SZ>(c, s);
      R.sr &= u16(~kV);
    } else if constexpr (K == Alu::MovFpe) {  // E-clock synchronised load; no flag change
      set_reg<Size::Byte>(c, rd, s);
    } else if constexpr (K == Alu::Adds || K == Alu::Subs) {
      // No flags; a byte source is sign-extended, Rd is always a word.
      u32 v = s;
      if constexpr (SZ == Size::Byte) v = u32(s16(s8(s))) & 0xFFFF;
      R.r[rd] = u16(K == Alu::Adds ? R.r[rd] + v : R.r[rd] - v);
    } else if constexpr (K == Alu::Ldc) {
      bool done = false;
      if constexpr (SZ == Size::Byte && (M == EaMode::PostInc || M == EaMode::PreDec)) {
        // A byte LDC through the stack is a word access: EP takes the EP:DP
        // pair from the whole word; other registers their byte from the odd
        // lane (which `s` already is).  Behaviour taken from firmware that
        // relies on LDC.B @SP+,EP restoring both pages.
        if ((ip->r >> 4) == 7 && rd == 4) {
          const u16 sp_word = (M == EaMode::PostInc) ? u16(R.r[7] - 2) : R.r[7];
          const u32 w = c.mem_read16(c.stack_addr(sp_word));
          R.ep = u8(w >> 8);
          R.dp = u8(w);
          done = true;
        }
      }
      if (!done) c.write_cr(u8(rd), s, SZ);
      c.raise(Cpu::kPendDefer);
    } else if constexpr (K == Alu::Mulxu) {
      if constexpr (SZ == Size::Byte) {
        const u32 p = (R.r[rd] & 0xFF) * s;  // 8 x 8 -> 16
        R.r[rd] = u16(p);
        set_nz<Size::Word>(c, p);
      } else {
        const u32 p = u32(R.r[rd]) * s;      // 16 x 16 -> 32 in Rd:Rd+1 (d even)
        R.r[rd] = u16(p >> 16);
        R.r[(rd + 1) & 7] = u16(p);
        u16 f = u16(R.sr & ~(kN | kZ));
        if (p & 0x80000000u) f |= kN;
        if (p == 0) f |= kZ;
        R.sr = f;
      }
      R.sr &= u16(~(kV | kC));
    } else if constexpr (K == Alu::Divxu) {
      if (H8_UNLIKELY(s == 0)) {
        // Zero divide: N=V=C=0, Z=1, then exception (PC = next instruction).
        // The zero-divide timing row includes the exception sequence.
        R.sr = u16((R.sr & ~(kN | kV | kC)) | kZ);
        c.enter_exception(Cpu::kVecZeroDivide, pc_next(c, ip), -1);
        states = ip->cyc2 + stack_pen(c, ip->icnt2);
        next = c.cells_for(R.cp, R.pc);
      } else if constexpr (SZ == Size::Byte) {
        const u32 dividend = R.r[rd];
        const u32 q = dividend / s, rem = dividend % s;
        if (q > 0xFF) {  // overflow: V=1, division not performed (row = normal - 12)
          R.sr = u16((R.sr & ~(kN | kZ | kC)) | kV);
          states -= 12;
        } else {
          R.r[rd] = u16((rem << 8) | q);
          set_nz<Size::Byte>(c, q);
          R.sr &= u16(~(kV | kC));
        }
      } else {
        const u32 dividend = (u32(R.r[rd]) << 16) | R.r[(rd + 1) & 7];
        const u32 q = dividend / s, rem = dividend % s;
        if (q > 0xFFFF) {  // overflow (row = normal - 18)
          R.sr = u16((R.sr & ~(kN | kZ | kC)) | kV);
          states -= 18;
        } else {
          R.r[rd] = u16(rem);
          R.r[(rd + 1) & 7] = u16(q);
          set_nz<Size::Word>(c, q);
          R.sr &= u16(~(kV | kC));
        }
      }
    }
    H8_END(c, next, states);
  }

  // ---------------------------------------------------------------------------
  // <EAd> operations

  template <Rmw K, Size SZ, EaMode M>
  static const Cell* h_rmw(Cpu& c, const Cell* ip) {
    constexpr u32 m = size_mask(SZ), msb = size_msb(SZ);
    Cpu::Regs& R = c.regs_;
    const Opnd<M, SZ> o(c, ip);
    ++c.insn_count_;

    if constexpr (K == Rmw::AddQ) {
      const u32 v = o.read(), s = u32(s16(op_imm(ip))) & m, r = v + s;
      flags_add<SZ>(c, v, s, r, false);
      o.write(r);
    } else if constexpr (K == Rmw::Neg) {
      const u32 v = o.read(), r = 0u - v;
      flags_sub<SZ>(c, 0, v, r, false);
      o.write(r);
    } else if constexpr (K == Rmw::Not) {
      const u32 r = ~o.read() & m;
      o.write(r);
      set_nz<SZ>(c, r);
      R.sr &= u16(~kV);
    } else if constexpr (K == Rmw::Clr) {
      o.write(0);
      R.sr = u16((R.sr & ~(kN | kV | kC)) | kZ);
    } else if constexpr (K == Rmw::Tst) {
      set_nz<SZ>(c, o.read());
      R.sr &= u16(~(kV | kC));
    } else if constexpr (K == Rmw::Tas) {
      const u32 v = o.read();
      set_nz<SZ>(c, v);
      R.sr &= u16(~(kV | kC));
      o.write(v | 0x80);
    } else if constexpr (K >= Rmw::Shal && K <= Rmw::Rotxr) {
      const u32 v = o.read();
      const bool oldc = R.sr & kC;
      u32 r = 0;
      bool cf = false, vf = false;
      if constexpr (K == Rmw::Shal) { cf = v & msb; r = (v << 1) & m; vf = ((v ^ r) & msb) != 0; }
      else if constexpr (K == Rmw::Shar) { cf = v & 1; r = (v >> 1) | (v & msb); }
      else if constexpr (K == Rmw::Shll) { cf = v & msb; r = (v << 1) & m; }
      else if constexpr (K == Rmw::Shlr) { cf = v & 1; r = v >> 1; }
      else if constexpr (K == Rmw::Rotl) { cf = v & msb; r = ((v << 1) | (cf ? 1u : 0u)) & m; }
      else if constexpr (K == Rmw::Rotr) { cf = v & 1; r = (v >> 1) | (cf ? msb : 0u); }
      else if constexpr (K == Rmw::Rotxl) { cf = v & msb; r = ((v << 1) | (oldc ? 1u : 0u)) & m; }
      else { cf = v & 1; r = (v >> 1) | (oldc ? msb : 0u); }  // Rotxr
      u16 f = u16(R.sr & ~(kN | kZ | kV | kC));
      if (r & msb) f |= kN;
      if (r == 0) f |= kZ;
      if (vf) f |= kV;
      if (cf) f |= kC;
      R.sr = f;
      o.write(r);
    } else if constexpr (K >= Rmw::BsetI && K <= Rmw::BtstR) {
      constexpr bool from_reg = K >= Rmw::BsetR;
      // Bit numbers 8-15 on a byte operand address bits that are always 0:
      // the test reads Z = 1 and set/clear/not have no effect on the byte.
      const unsigned bit = from_reg ? (R.r[ip->r & 7] & 0x0F) : (ip->x >> 4);
      const u32 mask = 1u << bit;
      const u32 v = o.read();
      if (v & mask) R.sr &= u16(~kZ); else R.sr |= kZ;
      if constexpr (K == Rmw::BsetI || K == Rmw::BsetR) o.write(v | mask);
      else if constexpr (K == Rmw::BclrI || K == Rmw::BclrR) o.write(v & ~mask);
      else if constexpr (K == Rmw::BnotI || K == Rmw::BnotR) o.write(v ^ mask);
    } else if constexpr (K == Rmw::CmpImm) {
      const u32 v = o.read(), s = u32(op_imm(ip)) & m;
      flags_sub<SZ>(c, v, s, v - s, false);
    } else if constexpr (K == Rmw::MovImm) {
      const u32 v = u32(op_imm(ip)) & m;
      o.write(v);
      set_nz<SZ>(c, v);
      R.sr &= u16(~kV);
    } else if constexpr (K == Rmw::MovStore) {
      const u32 v = get_reg<SZ>(c, ip->r & 7);
      o.write(v);
      set_nz<SZ>(c, v);
      R.sr &= u16(~kV);
    } else if constexpr (K == Rmw::MovTpe) {  // E-clock synchronised store; no flag change
      o.write(R.r[ip->r & 7] & 0xFF);
    } else if constexpr (K == Rmw::Stc) {
      const u8 cr = u8(ip->r & 7);
      bool done = false;
      if constexpr (SZ == Size::Byte && (M == EaMode::PreDec || M == EaMode::PostInc)) {
        // Byte STC through the stack is a word access: EP stores the EP:DP
        // pair, other registers their byte duplicated into both lanes.
        if ((ip->r >> 4) == 7) {
          const u16 sp_word = (M == EaMode::PostInc) ? u16(R.r[7] - 2) : R.r[7];
          const u32 b = c.read_cr(cr, Size::Byte) & 0xFF;
          const u32 w = (cr == 4) ? ((u32(R.ep) << 8) | R.dp) : ((b << 8) | b);
          c.mem_write16(c.stack_addr(sp_word), w);
          done = true;
        }
      }
      if (!done) o.write(c.read_cr(cr, SZ) & m);
    }
    const Cell* next = seq(ip);
    const unsigned states = ip->cyc + ea_pen<M, SZ>(c, ip);
    H8_END(c, next, states);
  }

  // ---------------------------------------------------------------------------
  // Register-only instructions

  static const Cell* h_xch(Cpu& c, const Cell* ip) {
    Cpu::Regs& R = c.regs_;
    const unsigned rs = ip->r >> 4, rd = ip->r & 7;
    const u16 t = R.r[rs];
    R.r[rs] = R.r[rd];
    R.r[rd] = t;
    ++c.insn_count_;
    H8_END(c, seq(ip), ip->cyc);
  }
  static const Cell* h_swap(Cpu& c, const Cell* ip) {
    Cpu::Regs& R = c.regs_;
    const unsigned rd = ip->r & 7;
    const u16 v = R.r[rd];
    R.r[rd] = u16((v << 8) | (v >> 8));
    // N/Z reflect the whole 16-bit register after the exchange (as EXTS/EXTU do).
    set_nz<Size::Word>(c, R.r[rd]);
    R.sr &= u16(~kV);
    ++c.insn_count_;
    H8_END(c, seq(ip), ip->cyc);
  }
  template <bool Signed>
  static const Cell* h_ext(Cpu& c, const Cell* ip) {
    Cpu::Regs& R = c.regs_;
    const unsigned rd = ip->r & 7;
    R.r[rd] = Signed ? u16(s16(s8(R.r[rd]))) : u16(R.r[rd] & 0x00FF);
    set_nz<Size::Word>(c, R.r[rd]);
    R.sr &= u16(~(kV | kC));
    ++c.insn_count_;
    H8_END(c, seq(ip), ip->cyc);
  }
  template <bool Sub>
  static const Cell* h_decimal(Cpu& c, const Cell* ip) {
    Cpu::Regs& R = c.regs_;
    const unsigned rs = ip->r >> 4, rd = ip->r & 7;
    bool out;
    const u8 r = Sub ? bcd_sub(u8(R.r[rd]), u8(R.r[rs]), R.sr & kC, out)
                     : bcd_add(u8(R.r[rd]), u8(R.r[rs]), R.sr & kC, out);
    set_reg<Size::Byte>(c, rd, r);
    u16 f = u16(R.sr & ~(kZ | kC));
    if (r == 0 && (R.sr & kZ)) f |= kZ;
    if (out) f |= kC;
    R.sr = f;
    ++c.insn_count_;
    H8_END(c, seq(ip), ip->cyc);
  }

  // ---------------------------------------------------------------------------
  // System control

  template <Size SZ, int K>  // K: 0 = ANDC, 1 = ORC, 2 = XORC
  static const Cell* h_logic_cr(Cpu& c, const Cell* ip) {
    const u8 cr = u8(ip->r & 7);
    const u32 imm = u32(ip->imm) & size_mask(SZ);
    const u32 cur = c.read_cr(cr, SZ);
    const u32 r = K == 0 ? (cur & imm) : K == 1 ? (cur | imm) : (cur ^ imm);
    c.write_cr(cr, r, SZ);
    if (cr != 0 && cr != 1) {  // CR is not SR/CCR: N,Z from result, V=0
      set_nz<SZ>(c, r);
      c.regs_.sr &= u16(~kV);
    }
    c.raise(Cpu::kPendDefer);
    ++c.insn_count_;
    H8_END(c, seq(ip), ip->cyc);
  }
  static const Cell* h_trapa(Cpu& c, const Cell* ip) {
    ++c.insn_count_;
    const u8 vector = u8(Cpu::kVecTrapaBase + (ip->r & 0x0F));
    if (c.trapa_hook_ && !c.trapa_hook_(vector)) {  // swallowed by the host
      H8_END(c, seq(ip), ip->cyc);
    }
    c.enter_exception(vector, pc_next(c, ip), -1);
    const Cell* next = c.cells_for(c.regs_.cp, c.regs_.pc);
    H8_END(c, next, ip->cyc + stack_pen(c, ip->icnt));
  }
  static const Cell* h_trapvs(Cpu& c, const Cell* ip) {
    ++c.insn_count_;
    if (c.regs_.sr & kV) {
      c.enter_exception(Cpu::kVecTrapVs, pc_next(c, ip), -1);
      const Cell* next = c.cells_for(c.regs_.cp, c.regs_.pc);
      H8_END(c, next, ip->cyc2 + stack_pen(c, ip->icnt2));
    }
    H8_END(c, seq(ip), ip->cyc);
  }
  static const Cell* h_rte(Cpu& c, const Cell* ip) {
    Cpu::Regs& R = c.regs_;
    c.write_cr(0, c.pop16(), Size::Word);
    if (c.max_mode_) R.cp = u8(c.pop16());
    R.pc = c.pop16();
    c.raise(Cpu::kPendDefer);
    ++c.insn_count_;
    const Cell* next = c.cells_for(R.cp, R.pc);
    H8_END(c, next, ip->cyc + stack_pen(c, ip->icnt));
  }
  static const Cell* h_link(Cpu& c, const Cell* ip) {
    Cpu::Regs& R = c.regs_;
    c.push16(R.r[6]);
    R.r[6] = R.r[7];
    R.r[7] = u16(R.r[7] + s16(ip->imm));
    ++c.insn_count_;
    H8_END(c, seq(ip), ip->cyc + stack_pen(c, ip->icnt));
  }
  static const Cell* h_unlk(Cpu& c, const Cell* ip) {
    Cpu::Regs& R = c.regs_;
    R.r[7] = R.r[6];
    R.r[6] = c.pop16();
    ++c.insn_count_;
    H8_END(c, seq(ip), ip->cyc + stack_pen(c, ip->icnt));
  }
  static const Cell* h_sleep(Cpu& c, const Cell* ip) {
    c.sleeping_ = true;
    c.raise(Cpu::kPendSleep);
    ++c.insn_count_;
    H8_END(c, seq(ip), ip->cyc);
  }
  static const Cell* h_nop(Cpu& c, const Cell* ip) {
    ++c.insn_count_;
    H8_END(c, seq(ip), ip->cyc);
  }
  static const Cell* h_ldm(Cpu& c, const Cell* ip) {
    // Lowest-numbered register first.  R7 in the list: dummy read, SP still
    // ends at (SP before) + 2 * n.
    Cpu::Regs& R = c.regs_;
    const unsigned list = ip->imm & 0xFF;
    for (unsigned i = 0; i < 8; ++i) {
      if (!(list & (1u << i))) continue;
      const u16 v = c.pop16();
      if (i != 7) R.r[i] = v;
    }
    ++c.insn_count_;
    H8_END(c, seq(ip), ip->cyc + stack_pen(c, ip->icnt));
  }
  static const Cell* h_stm(Cpu& c, const Cell* ip) {
    // Highest-numbered register first.  R7 in the list pushes (SP before) - 2.
    Cpu::Regs& R = c.regs_;
    const unsigned list = ip->imm & 0xFF;
    for (int i = 7; i >= 0; --i) {
      if (!(list & (1u << i))) continue;
      if (i == 7) {
        R.r[7] = u16(R.r[7] - 2);
        c.mem_write16(c.stack_addr(R.r[7]), R.r[7]);
      } else {
        c.push16(R.r[i]);
      }
    }
    ++c.insn_count_;
    H8_END(c, seq(ip), ip->cyc + stack_pen(c, ip->icnt));
  }

  // ---------------------------------------------------------------------------
  // Branches

  template <Cc CC>
  static const Cell* h_bcc(Cpu& c, const Cell* ip) {
    ++c.insn_count_;
    if (cond_true<CC>(c.regs_.sr)) {
      const u16 target = u16(pc_next(c, ip) + s16(ip->imm));
      H8_END(c, here(c, target), ip->cyc2);
    }
    H8_END(c, seq(ip), ip->cyc);
  }
  template <EaMode M, bool Sub>
  static const Cell* h_jmp(Cpu& c, const Cell* ip) {
    const unsigned n = ip->r >> 4;
    u16 target;
    if constexpr (M == EaMode::Abs16) target = ip->imm;
    else if constexpr (M == EaMode::RegInd) target = c.regs_.r[n];
    else target = u16(c.regs_.r[n] + s16(ip->imm));
    unsigned states = ip->cyc;
    if constexpr (Sub) {
      c.push16(pc_next(c, ip));
      states += stack_pen(c, ip->icnt);
    }
    ++c.insn_count_;
    H8_END(c, here(c, target), states);
  }
  static const Cell* h_bsr(Cpu& c, const Cell* ip) {
    const u16 ret = pc_next(c, ip);
    c.push16(ret);
    ++c.insn_count_;
    H8_END(c, here(c, u16(ret + s16(ip->imm))), ip->cyc + stack_pen(c, ip->icnt));
  }
  template <bool Dealloc>
  static const Cell* h_rts(Cpu& c, const Cell* ip) {
    const u16 target = c.pop16();
    if constexpr (Dealloc) c.regs_.r[7] = u16(c.regs_.r[7] + s16(ip->imm));
    ++c.insn_count_;
    H8_END(c, here(c, target), ip->cyc + stack_pen(c, ip->icnt));
  }
  static const Cell* h_scb(Cpu& c, const Cell* ip) {
    Cpu::Regs& R = c.regs_;
    const unsigned rn = ip->r & 7;
    bool exit_loop;
    switch (ScbCond(ip->x >> 4)) {
      case ScbCond::NE: exit_loop = !(R.sr & kZ); break;
      case ScbCond::EQ: exit_loop = (R.sr & kZ) != 0; break;
      default: exit_loop = false; break;
    }
    ++c.insn_count_;
    if (exit_loop) { H8_END(c, seq(ip), ip->cyc); }
    R.r[rn] = u16(R.r[rn] - 1);
    if (R.r[rn] == 0xFFFF) { H8_END(c, seq(ip), ip->cyc + 1u); }  // count = -1: one more state, not taken
    const u16 target = u16(pc_next(c, ip) + s16(ip->imm));
    H8_END(c, here(c, target), ip->cyc2);
  }
  template <EaMode M, bool Sub>
  static const Cell* h_pjmp(Cpu& c, const Cell* ip) {
    Cpu::Regs& R = c.regs_;
    u8 page;
    u16 target;
    if constexpr (M == EaMode::Abs24) { page = ip->r; target = ip->imm; }
    else { const unsigned n = ip->r >> 4; page = u8(R.r[n]); target = R.r[(n + 1) & 7]; }  // n must be even
    unsigned states = ip->cyc;
    if constexpr (Sub) {
      c.push16(pc_next(c, ip));
      c.push16(R.cp);
      states += stack_pen(c, ip->icnt);
    }
    R.cp = page;
    R.pc = target;
    ++c.insn_count_;
    const Cell* next = c.cells_for(page, target);
    H8_END(c, next, states);
  }
  template <bool Dealloc>
  static const Cell* h_prts(Cpu& c, const Cell* ip) {
    Cpu::Regs& R = c.regs_;
    R.cp = u8(c.pop16());
    R.pc = c.pop16();
    if constexpr (Dealloc) R.r[7] = u16(R.r[7] + s16(ip->imm));
    ++c.insn_count_;
    const Cell* next = c.cells_for(R.cp, R.pc);
    H8_END(c, next, ip->cyc + stack_pen(c, ip->icnt));
  }

  // ---------------------------------------------------------------------------
  // Exceptional cells

  // Undefined operation code / addressing mode, or maximum-mode-only
  // instruction in minimum mode.  The PC pushed is the instruction start.
  static const Cell* h_invalid(Cpu& c, const Cell* ip) {
    c.enter_exception(Cpu::kVecInvalidInsn, pc_of(c, ip), -1);
    const Cell* next = c.cells_for(c.regs_.cp, c.regs_.pc);
    H8_END(c, next, c.exception_states() + stack_pen(c, ip->icnt));
  }
  // Instruction prefetch from a no-execute area (register field / external I/O).
  static const Cell* h_noexec(Cpu& c, const Cell* ip) {
    c.enter_exception(Cpu::kVecAddressError, pc_of(c, ip), -1);
    const Cell* next = c.cells_for(c.regs_.cp, c.regs_.pc);
    H8_END(c, next, c.exception_states() + stack_pen(c, ip->icnt));
  }

  // ---------------------------------------------------------------------------
  // Fill: decode the instruction at this cell and install its handler.

  static const Cell* fill(Cpu& c, const Cell* ip) {
    const u16 pc = pc_of(c, ip);
    const u32 fa = c.code_addr(pc) & c.bus_.addr_mask();
    const u8 at = c.bus_.attr(fa);
    Cell cell{};
    cell.x = 1;
    cell.icnt = u8(c.max_mode_ ? 10 : 6);  // stacked bytes + vector read of an exception entry
    if (at & Bus::kNoExec) {
      cell.fn = &h_noexec;
    } else {
      const DecodedInsn d = c.decode_at(fa);
      if (!d.valid() || (d.has(insn_flags::kMaxModeOnly) && !c.max_mode_)) {
        cell.fn = &h_invalid;
      } else {
        build(c, d, pc, at, cell);
        // RAM lines holding code become write-slow so writes can invalidate.
        c.bus_.mark_code(fa);
        c.bus_.mark_code(c.code_addr(u16(pc + d.length - 1)));
      }
    }
    *const_cast<Cell*>(ip) = cell;
    H8_GOTO(c, ip);
  }

  struct Static { u8 states, I; };
  // Static part of the state count: base + parity adjustment + fetch penalty
  // (+ fetch wait states).  Only the operand-class penalty is left for run time.
  static Static static_states(const Cpu& c, const DecodedInsn& d, u16 pc, u8 fetch_attr, ExecCond cond, u8 n_regs) {
    TimingContext ctx;
    ctx.max_mode = c.max_mode_;
    ctx.cond = cond;
    ctx.n_regs = n_regs;
    const BaseTiming b = base_timing(d, ctx);
    const BusClass fc = BusClass(fetch_attr & Bus::kClassMask);
    const unsigned wait = fetch_attr >> Bus::kWaitShift;
    unsigned st = b.states;
    if (bus_is_16bit(fc)) st += parity_adjustment(d, (pc & 1) != 0, cond);
    st += fetch_penalty(fc, b.JK);
    if (wait) st += wait * fetch_bus_cycles(fc, b.JK);
    return Static{u8(st), b.I};
  }

  static void build(const Cpu& c, const DecodedInsn& d, u16 pc, u8 fetch_attr, Cell& cell) {
    using namespace insn_flags;
    cell.fn = select(d);
    cell.r = u8((d.ea_reg << 4) | (d.reg & 0x0F));
    cell.x = u8((d.length & 7) | ((d.aux & 0x0F) << 4));

    // Parameters.
    switch (d.op) {
      case Op::Ldm: case Op::Stm:
        cell.imm = d.reg;  // register list
        break;
      case Op::Pjmp: case Op::Pjsr:
        if (d.ea == EaMode::Abs24) { cell.r = u8(u32(d.ea_ext) >> 16); cell.imm = u16(d.ea_ext); }
        break;
      default:
        if (d.ea == EaMode::Imm || d.ea == EaMode::None) cell.imm = u16(d.imm);
        else cell.imm = u16(d.ea_ext);
        break;
    }

    // Timing.
    const u8 n_regs = (d.op == Op::Ldm || d.op == Op::Stm) ? u8(std::popcount(unsigned(d.reg))) : 0;
    ExecCond primary = ExecCond::Normal, alternate = ExecCond::Normal;
    bool two = false;
    switch (d.op) {
      case Op::Bcc: case Op::Scb: case Op::TrapVs:
        primary = ExecCond::BranchNotTaken; alternate = ExecCond::BranchTaken; two = true; break;
      case Op::Divxu:
        alternate = ExecCond::DivZero; two = true; break;
      default: break;
    }
    const Static p = static_states(c, d, pc, fetch_attr, primary, n_regs);
    cell.cyc = p.states;
    cell.icnt = p.I;
    if (two) {
      const Static a = static_states(c, d, pc, fetch_attr, alternate, n_regs);
      cell.cyc2 = a.states;
      cell.icnt2 = a.I;
    }
    // MOV/CMP/ADD:Q #xx,<EAd> carry both an EA extension and an OP-side
    // immediate; the latter lives in the (unused) alternate-timing fields.
    if (d.has(kImmSrc)) {
      const u16 v = u16(d.imm);
      cell.cyc2 = u8(v >> 8);
      cell.icnt2 = u8(v);
    }
  }

  // ---------------------------------------------------------------------------
  // Handler selection

  template <Alu K, Size SZ>
  static Handler pick_alu(EaMode m) {
    switch (m) {
      case EaMode::Reg: return &h_alu<K, SZ, EaMode::Reg>;
      case EaMode::RegInd: return &h_alu<K, SZ, EaMode::RegInd>;
      case EaMode::Disp8: return &h_alu<K, SZ, EaMode::Disp8>;
      case EaMode::Disp16: return &h_alu<K, SZ, EaMode::Disp16>;
      case EaMode::PreDec: return &h_alu<K, SZ, EaMode::PreDec>;
      case EaMode::PostInc: return &h_alu<K, SZ, EaMode::PostInc>;
      case EaMode::Abs8: return &h_alu<K, SZ, EaMode::Abs8>;
      case EaMode::Abs16: return &h_alu<K, SZ, EaMode::Abs16>;
      case EaMode::Imm: return &h_alu<K, SZ, EaMode::Imm>;
      default: return &h_invalid;
    }
  }
  template <Alu K>
  static Handler pick_alu(const DecodedInsn& d) {
    return d.size == Size::Word ? pick_alu<K, Size::Word>(d.ea) : pick_alu<K, Size::Byte>(d.ea);
  }
  template <Rmw K, Size SZ>
  static Handler pick_rmw(EaMode m) {
    switch (m) {
      case EaMode::Reg: return &h_rmw<K, SZ, EaMode::Reg>;
      case EaMode::RegInd: return &h_rmw<K, SZ, EaMode::RegInd>;
      case EaMode::Disp8: return &h_rmw<K, SZ, EaMode::Disp8>;
      case EaMode::Disp16: return &h_rmw<K, SZ, EaMode::Disp16>;
      case EaMode::PreDec: return &h_rmw<K, SZ, EaMode::PreDec>;
      case EaMode::PostInc: return &h_rmw<K, SZ, EaMode::PostInc>;
      case EaMode::Abs8: return &h_rmw<K, SZ, EaMode::Abs8>;
      case EaMode::Abs16: return &h_rmw<K, SZ, EaMode::Abs16>;
      default: return &h_invalid;
    }
  }
  template <Rmw K>
  static Handler pick_rmw(const DecodedInsn& d) {
    return d.size == Size::Word ? pick_rmw<K, Size::Word>(d.ea) : pick_rmw<K, Size::Byte>(d.ea);
  }

  static Handler select(const DecodedInsn& d) {
    using namespace insn_flags;
    switch (d.op) {
      // ---- data transfer
      case Op::Mov:
        if (d.has(kImmSrc)) return pick_rmw<Rmw::MovImm>(d);
        if (d.has(kStore)) return pick_rmw<Rmw::MovStore>(d);
        return pick_alu<Alu::Mov>(d);
      case Op::MovFpe: return pick_alu<Alu::MovFpe, Size::Byte>(d.ea);
      case Op::MovTpe: return pick_rmw<Rmw::MovTpe, Size::Byte>(d.ea);
      case Op::Ldm: return &h_ldm;
      case Op::Stm: return &h_stm;
      case Op::Xch: return &h_xch;
      case Op::Swap: return &h_swap;
      // ---- arithmetic
      case Op::Add: return pick_alu<Alu::Add>(d);
      case Op::AddQ: return pick_rmw<Rmw::AddQ>(d);
      case Op::Adds: return pick_alu<Alu::Adds>(d);
      case Op::Addx: return pick_alu<Alu::Addx>(d);
      case Op::Dadd: return &h_decimal<false>;
      case Op::Sub: return pick_alu<Alu::Sub>(d);
      case Op::Subs: return pick_alu<Alu::Subs>(d);
      case Op::Subx: return pick_alu<Alu::Subx>(d);
      case Op::Dsub: return &h_decimal<true>;
      case Op::Mulxu: return pick_alu<Alu::Mulxu>(d);
      case Op::Divxu: return pick_alu<Alu::Divxu>(d);
      case Op::Cmp:
        if (d.has(kImmSrc)) return pick_rmw<Rmw::CmpImm>(d);
        return pick_alu<Alu::Cmp>(d);
      case Op::Exts: return &h_ext<true>;
      case Op::Extu: return &h_ext<false>;
      case Op::Tst: return pick_rmw<Rmw::Tst>(d);
      case Op::Neg: return pick_rmw<Rmw::Neg>(d);
      case Op::Clr: return pick_rmw<Rmw::Clr>(d);
      case Op::Tas: return pick_rmw<Rmw::Tas, Size::Byte>(d.ea);
      // ---- shift / rotate
      case Op::Shal: return pick_rmw<Rmw::Shal>(d);
      case Op::Shar: return pick_rmw<Rmw::Shar>(d);
      case Op::Shll: return pick_rmw<Rmw::Shll>(d);
      case Op::Shlr: return pick_rmw<Rmw::Shlr>(d);
      case Op::Rotl: return pick_rmw<Rmw::Rotl>(d);
      case Op::Rotr: return pick_rmw<Rmw::Rotr>(d);
      case Op::Rotxl: return pick_rmw<Rmw::Rotxl>(d);
      case Op::Rotxr: return pick_rmw<Rmw::Rotxr>(d);
      // ---- logic
      case Op::And: return pick_alu<Alu::And>(d);
      case Op::Or: return pick_alu<Alu::Or>(d);
      case Op::Xor: return pick_alu<Alu::Xor>(d);
      case Op::Not: return pick_rmw<Rmw::Not>(d);
      // ---- bit manipulation
      case Op::Bset: return d.has(kBitInReg) ? pick_rmw<Rmw::BsetR>(d) : pick_rmw<Rmw::BsetI>(d);
      case Op::Bclr: return d.has(kBitInReg) ? pick_rmw<Rmw::BclrR>(d) : pick_rmw<Rmw::BclrI>(d);
      case Op::Bnot: return d.has(kBitInReg) ? pick_rmw<Rmw::BnotR>(d) : pick_rmw<Rmw::BnotI>(d);
      case Op::Btst: return d.has(kBitInReg) ? pick_rmw<Rmw::BtstR>(d) : pick_rmw<Rmw::BtstI>(d);
      // ---- system control
      // Control-register transfers take their size from the Sz bit of the EA
      // field, not from the register: the "not allowed" word forms of the
      // byte registers are used by real firmware (LDC.W #xx:16,DP) and behave
      // as documented in Cpu::read_cr / write_cr.
      case Op::Ldc: return pick_alu<Alu::Ldc>(d);
      case Op::Stc: return pick_rmw<Rmw::Stc>(d);
      case Op::Andc: return d.size == Size::Word ? &h_logic_cr<Size::Word, 0> : &h_logic_cr<Size::Byte, 0>;
      case Op::Orc: return d.size == Size::Word ? &h_logic_cr<Size::Word, 1> : &h_logic_cr<Size::Byte, 1>;
      case Op::Xorc: return d.size == Size::Word ? &h_logic_cr<Size::Word, 2> : &h_logic_cr<Size::Byte, 2>;
      case Op::Trapa: return &h_trapa;
      case Op::TrapVs: return &h_trapvs;
      case Op::Rte: return &h_rte;
      case Op::Link: return &h_link;
      case Op::Unlk: return &h_unlk;
      case Op::Sleep: return &h_sleep;
      case Op::Nop: return &h_nop;
      // ---- branches
      case Op::Bcc:
        switch (Cc(d.reg & 0x0F)) {
          case Cc::T: return &h_bcc<Cc::T>;   case Cc::F: return &h_bcc<Cc::F>;
          case Cc::HI: return &h_bcc<Cc::HI>; case Cc::LS: return &h_bcc<Cc::LS>;
          case Cc::CC: return &h_bcc<Cc::CC>; case Cc::CS: return &h_bcc<Cc::CS>;
          case Cc::NE: return &h_bcc<Cc::NE>; case Cc::EQ: return &h_bcc<Cc::EQ>;
          case Cc::VC: return &h_bcc<Cc::VC>; case Cc::VS: return &h_bcc<Cc::VS>;
          case Cc::PL: return &h_bcc<Cc::PL>; case Cc::MI: return &h_bcc<Cc::MI>;
          case Cc::GE: return &h_bcc<Cc::GE>; case Cc::LT: return &h_bcc<Cc::LT>;
          case Cc::GT: return &h_bcc<Cc::GT>; case Cc::LE: return &h_bcc<Cc::LE>;
        }
        return &h_bcc<Cc::T>;
      case Op::Jmp:
        switch (d.ea) {
          case EaMode::Abs16: return &h_jmp<EaMode::Abs16, false>;
          case EaMode::RegInd: return &h_jmp<EaMode::RegInd, false>;
          case EaMode::Disp8: return &h_jmp<EaMode::Disp8, false>;
          default: return &h_jmp<EaMode::Disp16, false>;
        }
      case Op::Jsr:
        switch (d.ea) {
          case EaMode::Abs16: return &h_jmp<EaMode::Abs16, true>;
          case EaMode::RegInd: return &h_jmp<EaMode::RegInd, true>;
          case EaMode::Disp8: return &h_jmp<EaMode::Disp8, true>;
          default: return &h_jmp<EaMode::Disp16, true>;
        }
      case Op::Bsr: return &h_bsr;
      case Op::Rts: return &h_rts<false>;
      case Op::Rtd: return &h_rts<true>;
      case Op::Scb: return &h_scb;
      case Op::Pjmp: return d.ea == EaMode::Abs24 ? &h_pjmp<EaMode::Abs24, false> : &h_pjmp<EaMode::RegInd, false>;
      case Op::Pjsr: return d.ea == EaMode::Abs24 ? &h_pjmp<EaMode::Abs24, true> : &h_pjmp<EaMode::RegInd, true>;
      case Op::Prts: return &h_prts<false>;
      case Op::Prtd: return &h_prts<true>;
      case Op::Invalid:
      case Op::Count:
        break;
    }
    return &h_invalid;
  }
};

namespace detail {
const Cell* cell_fill(Cpu& cpu, const Cell* ip) {
#if H8_HAS_MUSTTAIL
  [[clang::musttail]] return ExecImpl::fill(cpu, ip);
#else
  return ExecImpl::fill(cpu, ip);
#endif
}
}  // namespace detail

}  // namespace h8500

// The SH family's instruction set, as the cores see it.
//
// One Op enumeration covers the SH-2 (SH7014/16/17 and the SH-1 subset) and
// the SH-3 (SH7709A); the SH-4 additions have room at the end.  A core says
// which of them it executes through its Isa (sh/decode.hpp) and shares the
// handlers for the ones that behave the same everywhere (sh/insn.hpp).  What
// each core keeps for itself is its exception model and the instructions
// that touch it.
#pragma once
#include "common/types.hpp"

namespace sh {

using emu::u8;
using emu::u16;
using emu::u32;
using emu::u64;
using emu::s8;
using emu::s16;
using emu::s32;
using emu::s64;

// X-macro over every instruction: the cores' handler tables are built from it.
#define SH_FOR_EACH_OP(X)                                                                                        \
  X(MovI) X(MovwPc) X(MovlPc) X(MovR)                                                                            \
  X(MovbS) X(MovwS) X(MovlS) X(MovbL) X(MovwL) X(MovlL)                                                          \
  X(MovbM) X(MovwM) X(MovlM) X(MovbP) X(MovwP) X(MovlP)                                                          \
  X(MovbS4) X(MovwS4) X(MovlS4) X(MovbL4) X(MovwL4) X(MovlL4)                                                    \
  X(MovbS0) X(MovwS0) X(MovlS0) X(MovbL0) X(MovwL0) X(MovlL0)                                                    \
  X(MovbSG) X(MovwSG) X(MovlSG) X(MovbLG) X(MovwLG) X(MovlLG)                                                    \
  X(Mova) X(Movt) X(SwapB) X(SwapW) X(Xtrct)                                                                     \
  X(Add) X(AddI) X(AddC) X(AddV) X(CmpEqI) X(CmpEq) X(CmpHs) X(CmpGe) X(CmpHi) X(CmpGt) X(CmpPl) X(CmpPz)        \
  X(CmpStr) X(Div1) X(Div0S) X(Div0U) X(DmulsL) X(DmuluL) X(Dt) X(ExtsB) X(ExtsW) X(ExtuB) X(ExtuW)             \
  X(MacL) X(MacW) X(MulL) X(MulsW) X(MuluW) X(Neg) X(NegC) X(Sub) X(SubC) X(SubV)                                \
  X(And) X(AndI) X(AndB) X(Not) X(Or) X(OrI) X(OrB) X(Tas) X(Tst) X(TstI) X(TstB) X(Xor) X(XorI) X(XorB)         \
  X(Rotl) X(Rotr) X(Rotcl) X(Rotcr) X(Shal) X(Shar) X(Shll) X(Shlr) X(Shll2) X(Shlr2) X(Shll8) X(Shlr8)          \
  X(Shll16) X(Shlr16)                                                                                            \
  X(Bf) X(BfS) X(Bt) X(BtS) X(Bra) X(Braf) X(Bsr) X(Bsrf) X(Jmp) X(Jsr) X(Rts)                                    \
  X(Clrt) X(Clrmac) X(LdcSr) X(LdcGbr) X(LdcVbr) X(LdclSr) X(LdclGbr) X(LdclVbr)                                 \
  X(LdsMach) X(LdsMacl) X(LdsPr) X(LdslMach) X(LdslMacl) X(LdslPr)                                               \
  X(Nop) X(Rte) X(Sett) X(Sleep) X(StcSr) X(StcGbr) X(StcVbr) X(StclSr) X(StclGbr) X(StclVbr)                    \
  X(StsMach) X(StsMacl) X(StsPr) X(StslMach) X(StslMacl) X(StslPr) X(Trapa)                                      \
  /* SH-3 additions */                                                                                           \
  X(Pref) X(Shad) X(Shld) X(Clrs) X(Sets) X(Ldtlb)                                                               \
  X(LdcSsr) X(LdcSpc) X(LdcRbank) X(LdclSsr) X(LdclSpc) X(LdclRbank)                                             \
  X(StcSsr) X(StcSpc) X(StcRbank) X(StclSsr) X(StclSpc) X(StclRbank)

enum class Op : u8 {
  Illegal = 0,
#define SH_OP_ENUM(name) name,
  SH_FOR_EACH_OP(SH_OP_ENUM)
#undef SH_OP_ENUM
  kCount
};

struct DecodedInsn {
  Op op = Op::Illegal;
  u8 n = 0, m = 0;
  s32 imm = 0;    // immediate or displacement, scaled where the manual scales it
  u8 cycles = 1;  // issue cycles of the core that decoded it
};

// Delayed branch instructions (the following instruction is a delay slot).
inline constexpr bool is_delayed_branch(Op op) {
  switch (op) {
    case Op::BfS: case Op::BtS: case Op::Bra: case Op::Braf: case Op::Bsr: case Op::Bsrf:
    case Op::Jmp: case Op::Jsr: case Op::Rts: case Op::Rte: return true;
    default: return false;
  }
}
// Instructions that rewrite the PC: illegal in a delay slot.
inline constexpr bool writes_pc(Op op) {
  return is_delayed_branch(op) || op == Op::Bf || op == Op::Bt || op == Op::Trapa;
}
// SH-2 additions to the SH-1 instruction set (illegal on the SH7034).
inline constexpr bool is_sh2_only(Op op) {
  switch (op) {
    case Op::BfS: case Op::BtS: case Op::Braf: case Op::Bsrf: case Op::DmulsL: case Op::DmuluL: case Op::Dt:
    case Op::MacL: case Op::MulL: return true;
    default: return false;
  }
}
// SH-3 additions to the SH-2 instruction set.
inline constexpr bool is_sh3_only(Op op) { return op >= Op::Pref && op < Op::kCount; }
// Instructions that read or write memory (the PC-relative loads too), and
// so can reach a device: a core that reports the executing instruction's
// address to device hooks only has to note it for these.
inline constexpr bool accesses_memory(Op op) {
  switch (op) {
    case Op::MovwPc: case Op::MovlPc:
    case Op::MovbS: case Op::MovwS: case Op::MovlS: case Op::MovbL: case Op::MovwL: case Op::MovlL:
    case Op::MovbM: case Op::MovwM: case Op::MovlM: case Op::MovbP: case Op::MovwP: case Op::MovlP:
    case Op::MovbS4: case Op::MovwS4: case Op::MovlS4: case Op::MovbL4: case Op::MovwL4: case Op::MovlL4:
    case Op::MovbS0: case Op::MovwS0: case Op::MovlS0: case Op::MovbL0: case Op::MovwL0: case Op::MovlL0:
    case Op::MovbSG: case Op::MovwSG: case Op::MovlSG: case Op::MovbLG: case Op::MovwLG: case Op::MovlLG:
    case Op::MacL: case Op::MacW: case Op::AndB: case Op::OrB: case Op::XorB: case Op::TstB: case Op::Tas:
    case Op::LdclSr: case Op::LdclGbr: case Op::LdclVbr: case Op::LdclSsr: case Op::LdclSpc: case Op::LdclRbank:
    case Op::LdslMach: case Op::LdslMacl: case Op::LdslPr:
    case Op::StclSr: case Op::StclGbr: case Op::StclVbr: case Op::StclSsr: case Op::StclSpc: case Op::StclRbank:
    case Op::StslMach: case Op::StslMacl: case Op::StslPr:
    case Op::Rte: case Op::Trapa:
      return true;
    default: return false;
  }
}
// SH-2: instructions after which an interrupt is not accepted (table 5.9 note 2).
inline constexpr bool disables_interrupt(Op op) {
  switch (op) {
    case Op::LdcSr: case Op::LdcGbr: case Op::LdcVbr: case Op::LdclSr: case Op::LdclGbr: case Op::LdclVbr:
    case Op::StcSr: case Op::StcGbr: case Op::StcVbr: case Op::StclSr: case Op::StclGbr: case Op::StclVbr:
    case Op::LdsMach: case Op::LdsMacl: case Op::LdsPr: case Op::LdslMach: case Op::LdslMacl: case Op::LdslPr:
    case Op::StsMach: case Op::StsMacl: case Op::StsPr: case Op::StslMach: case Op::StslMacl: case Op::StslPr:
      return true;
    default: return false;
  }
}

// How MAC.L saturates with the S bit set.  The SH-2 manual's pseudo-code
// accumulates the low 16 bits of MACH and leaves H'00008000 in MACH on a
// negative overflow; SH-4 silicon (an SH7091, measured for QEMU) clamps the
// 64-bit accumulator to 48 bits and leaves H'FFFF8000, which is what the
// SH-3's manual describes as well.  A core states which it follows.
enum class MacLSaturation : u8 { ManualPseudoCode, SignExtended48 };

// SR bits every member of the family has, where it has them.
constexpr u32 kSrT = 1u << 0, kSrS = 1u << 1, kSrQ = 1u << 8, kSrM = 1u << 9;
constexpr u32 kSrIMask = 0xF0, kSrIShift = 4;

}  // namespace sh

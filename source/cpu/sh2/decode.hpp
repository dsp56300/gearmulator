// SH-2 instruction decoder (SH7014/16/17 hardware manual section 2.4).
//
// Every instruction is one 16-bit word; the decoder classifies it into an Op
// and extracts the register fields (n, m), the immediate / displacement (raw,
// already sign- or zero-extended as the instruction requires) and the base
// execution cycles from tables 2.12-2.17.
#pragma once
#include "cpu/sh2/types.hpp"

namespace sh2 {

enum class Op : u8 {
  Illegal = 0,
  // data transfer
  MovI, MovwPc, MovlPc, MovR,
  MovbS, MovwS, MovlS,        // Rm,@Rn
  MovbL, MovwL, MovlL,        // @Rm,Rn
  MovbM, MovwM, MovlM,        // Rm,@-Rn
  MovbP, MovwP, MovlP,        // @Rm+,Rn
  MovbS4, MovwS4, MovlS4,     // R0,@(disp,Rn) / Rm,@(disp,Rn)
  MovbL4, MovwL4, MovlL4,     // @(disp,Rm),R0 / @(disp,Rm),Rn
  MovbS0, MovwS0, MovlS0,     // Rm,@(R0,Rn)
  MovbL0, MovwL0, MovlL0,     // @(R0,Rm),Rn
  MovbSG, MovwSG, MovlSG,     // R0,@(disp,GBR)
  MovbLG, MovwLG, MovlLG,     // @(disp,GBR),R0
  Mova, Movt, SwapB, SwapW, Xtrct,
  // arithmetic
  Add, AddI, AddC, AddV, CmpEqI, CmpEq, CmpHs, CmpGe, CmpHi, CmpGt, CmpPl, CmpPz, CmpStr,
  Div1, Div0S, Div0U, DmulsL, DmuluL, Dt, ExtsB, ExtsW, ExtuB, ExtuW, MacL, MacW, MulL, MulsW, MuluW,
  Neg, NegC, Sub, SubC, SubV,
  // logic
  And, AndI, AndB, Not, Or, OrI, OrB, Tas, Tst, TstI, TstB, Xor, XorI, XorB,
  // shift
  Rotl, Rotr, Rotcl, Rotcr, Shal, Shar, Shll, Shlr, Shll2, Shlr2, Shll8, Shlr8, Shll16, Shlr16,
  // branch
  Bf, BfS, Bt, BtS, Bra, Braf, Bsr, Bsrf, Jmp, Jsr, Rts,
  // system
  Clrt, Clrmac, LdcSr, LdcGbr, LdcVbr, LdclSr, LdclGbr, LdclVbr, LdsMach, LdsMacl, LdsPr, LdslMach, LdslMacl, LdslPr,
  Nop, Rte, Sett, Sleep, StcSr, StcGbr, StcVbr, StclSr, StclGbr, StclVbr, StsMach, StsMacl, StsPr, StslMach, StslMacl, StslPr,
  Trapa,
  kCount
};

struct DecodedInsn {
  Op op = Op::Illegal;
  u8 n = 0, m = 0;
  s32 imm = 0;    // immediate or displacement, scaled where the manual scales it
  u8 cycles = 1;  // minimum execution cycles (table 2.12-2.17)
};

// Delayed branch instructions (the following instruction is a delay slot).
inline constexpr bool is_delayed_branch(Op op) {
  switch (op) {
    case Op::BfS: case Op::BtS: case Op::Bra: case Op::Braf: case Op::Bsr: case Op::Bsrf:
    case Op::Jmp: case Op::Jsr: case Op::Rts: case Op::Rte: return true;
    default: return false;
  }
}
// Instructions that rewrite the PC: illegal in a delay slot (table 5.8).
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
// Instructions after which an interrupt is not accepted (table 5.9 note 2).
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

inline DecodedInsn decode(u16 code) {
  DecodedInsn d;
  const u8 n = (code >> 8) & 0xF, m = (code >> 4) & 0xF, lo = code & 0xF;
  const u8 i8 = code & 0xFF;
  const s32 si8 = s8(i8);
  d.n = n;
  d.m = m;
  auto set = [&](Op op, s32 imm = 0, u8 cyc = 1) { d.op = op; d.imm = imm; d.cycles = cyc; };
  switch (code >> 12) {
    case 0x0:
      switch (lo) {
        case 0x2:
          if (m == 0) set(Op::StcSr); else if (m == 1) set(Op::StcGbr); else if (m == 2) set(Op::StcVbr);
          break;
        case 0x3:  // BSRF / BRAF Rm: the register is in bits 11-8
          if (m == 0) { d.m = n; set(Op::Bsrf, 0, 2); } else if (m == 2) { d.m = n; set(Op::Braf, 0, 2); }
          break;
        case 0x4: set(Op::MovbS0); break;
        case 0x5: set(Op::MovwS0); break;
        case 0x6: set(Op::MovlS0); break;
        case 0x7: set(Op::MulL, 0, 2); break;
        case 0x8:
          if (code == 0x0008) set(Op::Clrt); else if (code == 0x0018) set(Op::Sett); else if (code == 0x0028) set(Op::Clrmac);
          break;
        case 0x9:
          if (code == 0x0009) set(Op::Nop); else if (code == 0x0019) set(Op::Div0U); else if (m == 2) set(Op::Movt);
          break;
        case 0xA:
          if (m == 0) set(Op::StsMach); else if (m == 1) set(Op::StsMacl); else if (m == 2) set(Op::StsPr);
          break;
        case 0xB:
          if (code == 0x000B) set(Op::Rts, 0, 2); else if (code == 0x001B) set(Op::Sleep, 0, 3); else if (code == 0x002B) set(Op::Rte, 0, 4);
          break;
        case 0xC: set(Op::MovbL0); break;
        case 0xD: set(Op::MovwL0); break;
        case 0xE: set(Op::MovlL0); break;
        case 0xF: set(Op::MacL, 0, 3); break;
        default: break;
      }
      break;
    case 0x1: set(Op::MovlS4, s32(lo) * 4); break;
    case 0x2:
      switch (lo) {
        case 0x0: set(Op::MovbS); break;
        case 0x1: set(Op::MovwS); break;
        case 0x2: set(Op::MovlS); break;
        case 0x4: set(Op::MovbM); break;
        case 0x5: set(Op::MovwM); break;
        case 0x6: set(Op::MovlM); break;
        case 0x7: set(Op::Div0S); break;
        case 0x8: set(Op::Tst); break;
        case 0x9: set(Op::And); break;
        case 0xA: set(Op::Xor); break;
        case 0xB: set(Op::Or); break;
        case 0xC: set(Op::CmpStr); break;
        case 0xD: set(Op::Xtrct); break;
        case 0xE: set(Op::MuluW); break;
        case 0xF: set(Op::MulsW); break;
        default: break;
      }
      break;
    case 0x3:
      switch (lo) {
        case 0x0: set(Op::CmpEq); break;
        case 0x2: set(Op::CmpHs); break;
        case 0x3: set(Op::CmpGe); break;
        case 0x4: set(Op::Div1); break;
        case 0x5: set(Op::DmuluL, 0, 2); break;
        case 0x6: set(Op::CmpHi); break;
        case 0x7: set(Op::CmpGt); break;
        case 0x8: set(Op::Sub); break;
        case 0xA: set(Op::SubC); break;
        case 0xB: set(Op::SubV); break;
        case 0xC: set(Op::Add); break;
        case 0xD: set(Op::DmulsL, 0, 2); break;
        case 0xE: set(Op::AddC); break;
        case 0xF: set(Op::AddV); break;
        default: break;
      }
      break;
    case 0x4:
      if (lo == 0xF) { set(Op::MacW, 0, 3); break; }
      // Every other 0100 instruction has its single register in bits 11-8:
      // the JSR/JMP/LDC/LDS source register is that field too.
      d.m = n;
      switch (i8) {
        case 0x00: set(Op::Shll); break;
        case 0x01: set(Op::Shlr); break;
        case 0x02: set(Op::StslMach); break;
        case 0x03: set(Op::StclSr, 0, 2); break;
        case 0x04: set(Op::Rotl); break;
        case 0x05: set(Op::Rotr); break;
        case 0x06: set(Op::LdslMach); break;
        case 0x07: set(Op::LdclSr, 0, 3); break;
        case 0x08: set(Op::Shll2); break;
        case 0x09: set(Op::Shlr2); break;
        case 0x0A: set(Op::LdsMach); break;
        case 0x0B: set(Op::Jsr, 0, 2); break;
        case 0x0E: set(Op::LdcSr); break;
        case 0x10: set(Op::Dt); break;
        case 0x11: set(Op::CmpPz); break;
        case 0x12: set(Op::StslMacl); break;
        case 0x13: set(Op::StclGbr, 0, 2); break;
        case 0x15: set(Op::CmpPl); break;
        case 0x16: set(Op::LdslMacl); break;
        case 0x17: set(Op::LdclGbr, 0, 3); break;
        case 0x18: set(Op::Shll8); break;
        case 0x19: set(Op::Shlr8); break;
        case 0x1A: set(Op::LdsMacl); break;
        case 0x1B: set(Op::Tas, 0, 4); break;
        case 0x1E: set(Op::LdcGbr); break;
        case 0x20: set(Op::Shal); break;
        case 0x21: set(Op::Shar); break;
        case 0x22: set(Op::StslPr); break;
        case 0x23: set(Op::StclVbr, 0, 2); break;
        case 0x24: set(Op::Rotcl); break;
        case 0x25: set(Op::Rotcr); break;
        case 0x26: set(Op::LdslPr); break;
        case 0x27: set(Op::LdclVbr, 0, 3); break;
        case 0x28: set(Op::Shll16); break;
        case 0x29: set(Op::Shlr16); break;
        case 0x2A: set(Op::LdsPr); break;
        case 0x2B: set(Op::Jmp, 0, 2); break;
        case 0x2E: set(Op::LdcVbr); break;
        default: break;
      }
      break;
    case 0x5: set(Op::MovlL4, s32(lo) * 4); break;
    case 0x6:
      switch (lo) {
        case 0x0: set(Op::MovbL); break;
        case 0x1: set(Op::MovwL); break;
        case 0x2: set(Op::MovlL); break;
        case 0x3: set(Op::MovR); break;
        case 0x4: set(Op::MovbP); break;
        case 0x5: set(Op::MovwP); break;
        case 0x6: set(Op::MovlP); break;
        case 0x7: set(Op::Not); break;
        case 0x8: set(Op::SwapB); break;
        case 0x9: set(Op::SwapW); break;
        case 0xA: set(Op::NegC); break;
        case 0xB: set(Op::Neg); break;
        case 0xC: set(Op::ExtuB); break;
        case 0xD: set(Op::ExtuW); break;
        case 0xE: set(Op::ExtsB); break;
        case 0xF: set(Op::ExtsW); break;
        default: break;
      }
      break;
    case 0x7: set(Op::AddI, si8); break;
    case 0x8:
      switch (n) {
        case 0x0: d.n = m; set(Op::MovbS4, s32(lo)); break;       // R0,@(disp,Rn): register in the m field
        case 0x1: d.n = m; set(Op::MovwS4, s32(lo) * 2); break;
        case 0x4: set(Op::MovbL4, s32(lo)); break;                // @(disp,Rm),R0
        case 0x5: set(Op::MovwL4, s32(lo) * 2); break;
        case 0x8: set(Op::CmpEqI, si8); break;
        case 0x9: set(Op::Bt, si8 * 2, 3); break;
        case 0xB: set(Op::Bf, si8 * 2, 3); break;
        case 0xD: set(Op::BtS, si8 * 2, 2); break;
        case 0xF: set(Op::BfS, si8 * 2, 2); break;
        default: break;
      }
      break;
    case 0x9: set(Op::MovwPc, s32(i8) * 2); break;
    case 0xA: set(Op::Bra, s32(s16(u16(code << 4)) >> 4) * 2, 2); break;
    case 0xB: set(Op::Bsr, s32(s16(u16(code << 4)) >> 4) * 2, 2); break;
    case 0xC:
      switch (n) {
        case 0x0: set(Op::MovbSG, s32(i8)); break;
        case 0x1: set(Op::MovwSG, s32(i8) * 2); break;
        case 0x2: set(Op::MovlSG, s32(i8) * 4); break;
        case 0x3: set(Op::Trapa, s32(i8), 8); break;
        case 0x4: set(Op::MovbLG, s32(i8)); break;
        case 0x5: set(Op::MovwLG, s32(i8) * 2); break;
        case 0x6: set(Op::MovlLG, s32(i8) * 4); break;
        case 0x7: set(Op::Mova, s32(i8) * 4); break;
        case 0x8: set(Op::TstI, s32(i8)); break;
        case 0x9: set(Op::AndI, s32(i8)); break;
        case 0xA: set(Op::XorI, s32(i8)); break;
        case 0xB: set(Op::OrI, s32(i8)); break;
        case 0xC: set(Op::TstB, s32(i8), 3); break;
        case 0xD: set(Op::AndB, s32(i8), 3); break;
        case 0xE: set(Op::XorB, s32(i8), 3); break;
        case 0xF: set(Op::OrB, s32(i8), 3); break;
        default: break;
      }
      break;
    case 0xD: set(Op::MovlPc, s32(i8) * 4); break;
    case 0xE: set(Op::MovI, si8); break;
    default: break;
  }
  return d;
}

}  // namespace sh2

// SH instruction decoder.
//
// Every instruction is one 16-bit word; the decoder classifies it into an Op
// and extracts the register fields (n, m) and the immediate / displacement
// (already scaled and sign-extended as the instruction requires).  It knows
// the whole family's codes; a core's Isa says which of them it executes
// (the rest decode as illegal) and how many issue cycles each one takes:
//
//   struct Isa {
//     static constexpr bool legal(Op op);
//     static constexpr u8 cycles(Op op);
//   };
#pragma once
#include "cpu/sh/op.hpp"

namespace sh {

// The family's codes, without a core's view of them.
inline DecodedInsn decode_raw(u16 code) {
  DecodedInsn d;
  const u8 n = (code >> 8) & 0xF, m = (code >> 4) & 0xF, lo = code & 0xF;
  const u8 i8 = code & 0xFF;
  const s32 si8 = s8(i8);
  d.n = n;
  d.m = m;
  auto set = [&](Op op, s32 imm = 0) { d.op = op; d.imm = imm; };
  switch (code >> 12) {
    case 0x0:
      switch (lo) {
        case 0x2:
          switch (m) {
            case 0: set(Op::StcSr); break;
            case 1: set(Op::StcGbr); break;
            case 2: set(Op::StcVbr); break;
            case 3: set(Op::StcSsr); break;
            case 4: set(Op::StcSpc); break;
            default: if (m & 8) { d.m = m & 7; set(Op::StcRbank); } break;
          }
          break;
        case 0x3:  // BSRF / BRAF / PREF: register in bits 11-8
          d.m = n;
          switch (m) {
            case 0: set(Op::Bsrf); break;
            case 2: set(Op::Braf); break;
            case 8: set(Op::Pref); break;
            default: break;
          }
          break;
        case 0x4: set(Op::MovbS0); break;
        case 0x5: set(Op::MovwS0); break;
        case 0x6: set(Op::MovlS0); break;
        case 0x7: set(Op::MulL); break;
        case 0x8:
          switch (code) {
            case 0x0008: set(Op::Clrt); break;
            case 0x0018: set(Op::Sett); break;
            case 0x0028: set(Op::Clrmac); break;
            case 0x0038: set(Op::Ldtlb); break;
            case 0x0048: set(Op::Clrs); break;
            case 0x0058: set(Op::Sets); break;
            default: break;
          }
          break;
        case 0x9:
          if (code == 0x0009) set(Op::Nop); else if (code == 0x0019) set(Op::Div0U); else if (m == 2) set(Op::Movt);
          break;
        case 0xA:
          switch (m) {
            case 0: set(Op::StsMach); break;
            case 1: set(Op::StsMacl); break;
            case 2: set(Op::StsPr); break;
            default: break;
          }
          break;
        case 0xB:
          if (code == 0x000B) set(Op::Rts); else if (code == 0x001B) set(Op::Sleep); else if (code == 0x002B) set(Op::Rte);
          break;
        case 0xC: set(Op::MovbL0); break;
        case 0xD: set(Op::MovwL0); break;
        case 0xE: set(Op::MovlL0); break;
        case 0xF: set(Op::MacL); break;
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
        case 0x5: set(Op::DmuluL); break;
        case 0x6: set(Op::CmpHi); break;
        case 0x7: set(Op::CmpGt); break;
        case 0x8: set(Op::Sub); break;
        case 0xA: set(Op::SubC); break;
        case 0xB: set(Op::SubV); break;
        case 0xC: set(Op::Add); break;
        case 0xD: set(Op::DmulsL); break;
        case 0xE: set(Op::AddC); break;
        case 0xF: set(Op::AddV); break;
        default: break;
      }
      break;
    case 0x4:
      if (lo == 0xF) { set(Op::MacW); break; }
      if (lo == 0xC) { set(Op::Shad); break; }
      if (lo == 0xD) { set(Op::Shld); break; }
      // Bank-register forms: 0100nnnn1mmm0111 LDC.L @Rn+,Rm_BANK / 1110 LDC Rn,Rm_BANK / 1000 0011 STC.L Rm_BANK,@-Rn
      if ((m & 8) && lo == 0x7) { d.m = m & 7; set(Op::LdclRbank); break; }
      if ((m & 8) && lo == 0xE) { d.m = m & 7; set(Op::LdcRbank); break; }
      if ((m & 8) && lo == 0x3) { d.m = m & 7; set(Op::StclRbank); break; }
      // Every other 0100 instruction has its single register in bits 11-8:
      // the JSR/JMP/LDC/LDS source register is that field too.
      d.m = n;
      switch (i8) {
        case 0x00: set(Op::Shll); break;
        case 0x01: set(Op::Shlr); break;
        case 0x02: set(Op::StslMach); break;
        case 0x03: set(Op::StclSr); break;
        case 0x04: set(Op::Rotl); break;
        case 0x05: set(Op::Rotr); break;
        case 0x06: set(Op::LdslMach); break;
        case 0x07: set(Op::LdclSr); break;
        case 0x08: set(Op::Shll2); break;
        case 0x09: set(Op::Shlr2); break;
        case 0x0A: set(Op::LdsMach); break;
        case 0x0B: set(Op::Jsr); break;
        case 0x0E: set(Op::LdcSr); break;
        case 0x10: set(Op::Dt); break;
        case 0x11: set(Op::CmpPz); break;
        case 0x12: set(Op::StslMacl); break;
        case 0x13: set(Op::StclGbr); break;
        case 0x15: set(Op::CmpPl); break;
        case 0x16: set(Op::LdslMacl); break;
        case 0x17: set(Op::LdclGbr); break;
        case 0x18: set(Op::Shll8); break;
        case 0x19: set(Op::Shlr8); break;
        case 0x1A: set(Op::LdsMacl); break;
        case 0x1B: set(Op::Tas); break;
        case 0x1E: set(Op::LdcGbr); break;
        case 0x20: set(Op::Shal); break;
        case 0x21: set(Op::Shar); break;
        case 0x22: set(Op::StslPr); break;
        case 0x23: set(Op::StclVbr); break;
        case 0x24: set(Op::Rotcl); break;
        case 0x25: set(Op::Rotcr); break;
        case 0x26: set(Op::LdslPr); break;
        case 0x27: set(Op::LdclVbr); break;
        case 0x28: set(Op::Shll16); break;
        case 0x29: set(Op::Shlr16); break;
        case 0x2A: set(Op::LdsPr); break;
        case 0x2B: set(Op::Jmp); break;
        case 0x2E: set(Op::LdcVbr); break;
        case 0x33: set(Op::StclSsr); break;
        case 0x37: set(Op::LdclSsr); break;
        case 0x3E: set(Op::LdcSsr); break;
        case 0x43: set(Op::StclSpc); break;
        case 0x47: set(Op::LdclSpc); break;
        case 0x4E: set(Op::LdcSpc); break;
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
        case 0x9: set(Op::Bt, si8 * 2); break;
        case 0xB: set(Op::Bf, si8 * 2); break;
        case 0xD: set(Op::BtS, si8 * 2); break;
        case 0xF: set(Op::BfS, si8 * 2); break;
        default: break;
      }
      break;
    case 0x9: set(Op::MovwPc, s32(i8) * 2); break;
    case 0xA: set(Op::Bra, s32(s16(u16(code << 4)) >> 4) * 2); break;
    case 0xB: set(Op::Bsr, s32(s16(u16(code << 4)) >> 4) * 2); break;
    case 0xC:
      switch (n) {
        case 0x0: set(Op::MovbSG, s32(i8)); break;
        case 0x1: set(Op::MovwSG, s32(i8) * 2); break;
        case 0x2: set(Op::MovlSG, s32(i8) * 4); break;
        case 0x3: set(Op::Trapa, s32(i8)); break;
        case 0x4: set(Op::MovbLG, s32(i8)); break;
        case 0x5: set(Op::MovwLG, s32(i8) * 2); break;
        case 0x6: set(Op::MovlLG, s32(i8) * 4); break;
        case 0x7: set(Op::Mova, s32(i8) * 4); break;
        case 0x8: set(Op::TstI, s32(i8)); break;
        case 0x9: set(Op::AndI, s32(i8)); break;
        case 0xA: set(Op::XorI, s32(i8)); break;
        case 0xB: set(Op::OrI, s32(i8)); break;
        case 0xC: set(Op::TstB, s32(i8)); break;
        case 0xD: set(Op::AndB, s32(i8)); break;
        case 0xE: set(Op::XorB, s32(i8)); break;
        case 0xF: set(Op::OrB, s32(i8)); break;
        default: break;
      }
      break;
    case 0xD: set(Op::MovlPc, s32(i8) * 4); break;
    case 0xE: set(Op::MovI, si8); break;
    default: break;
  }
  return d;
}

// The core's view: what it does not execute is illegal, and its own cycle count.
template <class Isa>
inline DecodedInsn decode(u16 code) {
  DecodedInsn d = decode_raw(code);
  if (!Isa::legal(d.op)) d = DecodedInsn{};
  d.cycles = Isa::cycles(d.op);
  return d;
}

}  // namespace sh

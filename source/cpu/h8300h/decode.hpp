// H8/300H instruction decoder: one DecodedInsn per instruction, shared by the
// cache fill (exec.cpp) and the disassembler.  The accepted encodings and
// their lengths follow the H8/300H programming manual for everything the
// firmware in this tree can meet; H8S-only forms (LDM/STM, TAS, MAC,
// two-bit shifts) decode as invalid.
#pragma once
#include "cpu/h8300h/types.hpp"

namespace h8300h {

enum class Op : u8 {
  Invalid, Nop, Sleep,
  // moves: x = size (1/2/4); imm = immediate / address / displacement
  MovRR, MovImm, MovAbs8, MovAbs16, MovAbs24, MovInd, MovDisp16, MovDisp24, MovPost, Movfpe, Movtpe,
  // arithmetic / logic, register-register and immediate forms (x = size)
  Add, Sub, Cmp, And, Or, Xor, AddImm, SubImm, CmpImm, AndImm, OrImm, XorImm,
  Addx, Subx, AddxImm, SubxImm,
  Inc, Dec,          // x = size, imm = 1 / 2
  Adds, Subs,        // imm = 1 / 2 / 4
  Mulxu, Mulxs, Divxu, Divxs,  // x = size of the result (2 / 4)
  Neg, Not, Extu, Exts,        // x = size
  Shal, Shar, Shll, Shlr, Rotl, Rotr, Rotxl, Rotxr,  // x = size
  // bit manipulation: x = kind (BitKind) | (immediate bit << 4) | (register bit ? 0x80 : 0)
  BitReg,            // operand: register rd
  BitInd,            // operand: @ERd (rd)
  BitAbs,            // operand: @aa (imm)
  // control flow
  Bcc8, Bcc16,       // x = condition, imm = target
  JmpEr, JmpAbs, JmpInd8, JsrEr, JsrAbs, JsrInd8, Bsr8, Bsr16, Rts, Rte, Trapa,
  // CCR
  LdcImm, LdcReg, StcReg, LdcAbs16, LdcAbs24, StcAbs16, StcAbs24, LdcInd, StcInd, LdcDisp16, StcDisp16,
  LdcDisp24, StcDisp24, LdcPost, StcPre, Andc, Orc, Xorc,
  Daa, Das, Eepmov,  // x = 1 / 2
};

// Bit-manipulation kinds (BSET family): the low nibble of x.
enum class BitKind : u8 { Bset, Bnot, Bclr, Btst, Bst, Bist, Bor, Bior, Bxor, Bixor, Band, Biand, Bld, Bild };

struct DecodedInsn {
  Op op = Op::Invalid;
  u8 len = 2;        // bytes
  u8 rs = 0, rd = 0; // register nibbles as encoded (byte regs 0-15, word regs 0-15, long regs 0-7)
  u8 x = 0;
  u32 imm = 0;
  bool store = false;  // memory forms: register -> memory
  bool valid() const { return op != Op::Invalid; }
};

// The third / fourth bytes of a memory bit-manipulation instruction: the
// operation and its bit operand.  `writes` selects the group the first
// byte announced (BSET/BNOT/BCLR/BST vs the test group).
inline bool bit_op(DecodedInsn& d, u8 c, u8 dd, bool writes) {
  const u8 imm = (dd >> 4) & 7;
  const bool inv = (dd & 0x80) != 0;
  d.rs = dd >> 4;  // register-selected bit number
  BitKind k;
  bool reg = false;
  if (!writes) {
    if (inv && c == 0x73) return false;
    switch (c) {
      case 0x63: k = BitKind::Btst; reg = true; break;
      case 0x73: k = BitKind::Btst; break;
      case 0x74: k = inv ? BitKind::Bior : BitKind::Bor; break;
      case 0x75: k = inv ? BitKind::Bixor : BitKind::Bxor; break;
      case 0x76: k = inv ? BitKind::Biand : BitKind::Band; break;
      case 0x77: k = inv ? BitKind::Bild : BitKind::Bld; break;
      default: return false;
    }
  } else {
    if (inv && (c == 0x70 || c == 0x71 || c == 0x72)) return false;
    switch (c) {
      case 0x60: k = BitKind::Bset; reg = true; break;
      case 0x61: k = BitKind::Bnot; reg = true; break;
      case 0x62: k = BitKind::Bclr; reg = true; break;
      case 0x67: k = inv ? BitKind::Bist : BitKind::Bst; break;
      case 0x70: k = BitKind::Bset; break;
      case 0x71: k = BitKind::Bnot; break;
      case 0x72: k = BitKind::Bclr; break;
      default: return false;
    }
  }
  d.x = u8(u8(k) | (imm << 4) | (reg ? 0x80 : 0));
  return true;
}


// `fetch(i)` returns code byte i relative to the instruction start.
template <class Fetch>
DecodedInsn decode(Fetch fetch) {
  DecodedInsn d;
  auto fail = [&] { d.op = Op::Invalid; return d; };
  auto set = [&](Op op, u8 len) { d.op = op; d.len = len; };
  auto w16 = [&](unsigned at) { return u32((fetch(at) << 8) | fetch(at + 1)); };
  auto w32 = [&](unsigned at) { return (w16(at) << 16) | w16(at + 2); };
  auto se16 = [](u32 v) { return u32(s32(s16(v))); };

  const u8 b0 = fetch(0), b1 = fetch(1);
  const u8 hi = b0 >> 4, lo = b0 & 15;
  switch (hi) {
    case 0:
      switch (lo) {
        case 0: if (b1) return fail(); set(Op::Nop, 2); return d;
        case 1: {
          switch (b1 >> 4) {
            case 0: {  // MOV.L memory forms (01 00 69/6B/6D/6F/78)
              if (b1) return fail();
              const u8 c = fetch(2), dd = fetch(3);
              if ((c & 0xE8) != 0x68) return fail();
              if (c == 0x6B) {
                if (dd & 0x58) return fail();
                d.rd = dd & 7; d.store = (dd & 0x80) != 0; d.x = 4;
                if (dd & 0x20) { d.imm = w32(4); set(Op::MovAbs24, 8); } else { d.imm = se16(w16(4)); set(Op::MovAbs16, 6); }
                return d;
              }
              if (dd & 0x08) return fail();
              d.x = 4;
              if (c == 0x6D) {
                // @ERs+,ERd (load) / ERs,@-ERd (store): rs/rd as in the old core
                d.rd = dd & 7; d.rs = (dd >> 4) & 7; d.store = (dd & 0x80) != 0;
                set(Op::MovPost, 4);
                return d;
              }
              if (c == 0x69) { d.rd = dd & 7; d.rs = (dd >> 4) & 7; d.store = (dd & 0x80) != 0; set(Op::MovInd, 4); return d; }
              if (c == 0x6F) { d.rd = dd & 7; d.rs = (dd >> 4) & 7; d.store = (dd & 0x80) != 0; d.imm = se16(w16(4)); set(Op::MovDisp16, 6); return d; }
              if (c == 0x78) {
                const u8 e = fetch(4), f = fetch(5);
                if (e != 0x6B) return fail();
                if ((f & 0x58) || !(f & 0x20)) return fail();
                d.rs = (dd >> 4) & 7; d.rd = f & 7; d.store = (f & 0x80) != 0; d.imm = w32(6);
                set(Op::MovDisp24, 10);
                return d;
              }
              return fail();
            }
            case 4: {  // LDC/STC memory forms (01 40 ...)
              if (b1 != 0x40) return fail();
              const u8 c = fetch(2), dd = fetch(3);
              if (c == 0x6B) {
                if (dd & 0x5F) return fail();
                d.store = (dd & 0x80) != 0;
                if (dd & 0x20) { d.imm = w32(4); set(d.store ? Op::StcAbs24 : Op::LdcAbs24, 8); }
                else { d.imm = se16(w16(4)); set(d.store ? Op::StcAbs16 : Op::LdcAbs16, 6); }
                return d;
              }
              if (dd & 0x0F) return fail();
              d.rs = (dd >> 4) & 7;
              if (c == 0x6D) { d.store = (dd & 0x80) != 0; set(d.store ? Op::StcPre : Op::LdcPost, 4); return d; }
              if (c == 0x69) { d.store = (dd & 0x80) != 0; set(d.store ? Op::StcInd : Op::LdcInd, 4); return d; }
              if (c == 0x6F) { d.store = (dd & 0x80) != 0; d.imm = se16(w16(4)); set(d.store ? Op::StcDisp16 : Op::LdcDisp16, 6); return d; }
              if (c == 0x78) {
                const u8 e = fetch(4), f = fetch(5);
                if (e != 0x6B) return fail();
                if ((f & 0x5F) || !(f & 0x20)) return fail();
                d.store = (f & 0x80) != 0; d.imm = w32(6);
                set(d.store ? Op::StcDisp24 : Op::LdcDisp24, 10);
                return d;
              }
              return fail();
            }
            case 8: if (b1 != 0x80) return fail(); set(Op::Sleep, 2); return d;
            case 12: {  // 01 C0 50/52: MULXS
              if (b1 & 15) return fail();
              const u8 c = fetch(2), dd = fetch(3);
              if ((c >> 4) != 5) return fail();
              d.rs = dd >> 4; d.rd = dd & 15;
              if ((c & 15) == 0) { d.x = 2; set(Op::Mulxs, 4); return d; }
              if ((c & 15) == 2) { if (d.rd & 8) return fail(); d.x = 4; set(Op::Mulxs, 4); return d; }
              return fail();
            }
            case 13: {  // 01 D0 51/53: DIVXS
              if (b1 & 15) return fail();
              const u8 c = fetch(2), dd = fetch(3);
              if ((c >> 4) != 5) return fail();
              d.rs = dd >> 4; d.rd = dd & 15;
              if ((c & 15) == 1) { d.x = 2; set(Op::Divxs, 4); return d; }
              if ((c & 15) == 3) { if (d.rd & 8) return fail(); d.x = 4; set(Op::Divxs, 4); return d; }
              return fail();
            }
            case 15: {  // 01 F0 64/65/66: OR/XOR/AND.L
              if (b1 & 15) return fail();
              const u8 c = fetch(2), dd = fetch(3);
              if ((c >> 4) != 6) return fail();
              if (dd & 0x88) return fail();
              d.rs = dd >> 4; d.rd = dd & 15; d.x = 4;
              switch (c & 15) {
                case 4: set(Op::Or, 4); return d;
                case 5: set(Op::Xor, 4); return d;
                case 6: set(Op::And, 4); return d;
                default: return fail();
              }
            }
            default: return fail();
          }
        }
        case 2: if (b1 & 0xF0) return fail(); d.rd = b1; set(Op::StcReg, 2); return d;
        case 3: if (b1 & 0xF0) return fail(); d.rd = b1; set(Op::LdcReg, 2); return d;
        case 4: d.imm = b1; set(Op::Orc, 2); return d;
        case 5: d.imm = b1; set(Op::Xorc, 2); return d;
        case 6: d.imm = b1; set(Op::Andc, 2); return d;
        case 7: d.imm = b1; set(Op::LdcImm, 2); return d;
        case 8: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 1; set(Op::Add, 2); return d;
        case 9: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 2; set(Op::Add, 2); return d;
        case 10:
          if ((b1 >> 4) == 0) { d.rd = b1 & 15; d.x = 1; d.imm = 1; set(Op::Inc, 2); return d; }
          if (b1 & 0x80) { if (b1 & 8) return fail(); d.rs = (b1 >> 4) & 7; d.rd = b1 & 7; d.x = 4; set(Op::Add, 2); return d; }
          return fail();
        case 11:
          switch (b1 >> 4) {
            case 0: if (b1 & 8) return fail(); d.rd = b1 & 7; d.imm = 1; set(Op::Adds, 2); return d;
            case 8: if (b1 & 8) return fail(); d.rd = b1 & 7; d.imm = 2; set(Op::Adds, 2); return d;
            case 9: if (b1 & 8) return fail(); d.rd = b1 & 7; d.imm = 4; set(Op::Adds, 2); return d;
            case 5: d.rd = b1 & 15; d.x = 2; d.imm = 1; set(Op::Inc, 2); return d;
            case 13: d.rd = b1 & 15; d.x = 2; d.imm = 2; set(Op::Inc, 2); return d;
            case 7: if (b1 & 8) return fail(); d.rd = b1 & 15; d.x = 4; d.imm = 1; set(Op::Inc, 2); return d;
            case 15: if (b1 & 8) return fail(); d.rd = b1 & 15; d.x = 4; d.imm = 2; set(Op::Inc, 2); return d;
            default: return fail();
          }
        case 12: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 1; set(Op::MovRR, 2); return d;
        case 13: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 2; set(Op::MovRR, 2); return d;
        case 14: d.rs = b1 >> 4; d.rd = b1 & 15; set(Op::Addx, 2); return d;
        case 15:
          if ((b1 >> 4) == 0) { d.rd = b1 & 15; set(Op::Daa, 2); return d; }
          if (b1 & 0x80) { if (b1 & 8) return fail(); d.rs = (b1 >> 4) & 7; d.rd = b1 & 7; d.x = 4; set(Op::MovRR, 2); return d; }
          return fail();
      }
      break;
    case 1: {
      switch (lo) {
        case 0: case 1: case 2: case 3: {  // shifts and rotates
          const u8 top = (b1 >> 4) & 7;
          if (top == 2 || top == 6) return fail();
          if (b1 & 0x40) return fail();  // two-bit shifts are H8S
          d.x = u8(((b1 >> 4) & 3) + 1);   // 1, 2, 4 (3 cannot occur)
          if (d.x == 3) d.x = 4;
          d.rd = b1 & 15;
          if (d.x == 4 && (d.rd & 8)) return fail();
          const bool alt = (b1 & 0x80) != 0;
          static constexpr Op ops[4][2] = {{Op::Shll, Op::Shal}, {Op::Shlr, Op::Shar}, {Op::Rotxl, Op::Rotl}, {Op::Rotxr, Op::Rotr}};
          set(ops[lo][alt ? 1 : 0], 2);
          return d;
        }
        case 4: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 1; set(Op::Or, 2); return d;
        case 5: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 1; set(Op::Xor, 2); return d;
        case 6: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 1; set(Op::And, 2); return d;
        case 7: {
          const u8 t = b1 >> 4;
          d.rd = b1 & 15;
          d.x = u8((t & 3) + 1);
          if (d.x == 3) return fail();
          if (d.x == 4 && (d.rd & 8)) return fail();
          switch (t) {
            case 0: case 1: case 3: set(Op::Not, 2); return d;
            case 5: case 7: set(Op::Extu, 2); return d;
            case 8: case 9: case 11: set(Op::Neg, 2); return d;
            case 13: case 15: set(Op::Exts, 2); return d;
            default: return fail();
          }
        }
        case 8: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 1; set(Op::Sub, 2); return d;
        case 9: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 2; set(Op::Sub, 2); return d;
        case 10:
          if (b1 & 0x80) { if (b1 & 8) return fail(); d.rs = (b1 >> 4) & 7; d.rd = b1 & 7; d.x = 4; set(Op::Sub, 2); return d; }
          if (!(b1 & 0xF0)) { d.rd = b1 & 15; d.x = 1; d.imm = 1; set(Op::Dec, 2); return d; }
          return fail();
        case 11:
          switch (b1 >> 4) {
            case 0: if (b1 & 8) return fail(); d.rd = b1 & 7; d.imm = 1; set(Op::Subs, 2); return d;
            case 8: if (b1 & 8) return fail(); d.rd = b1 & 7; d.imm = 2; set(Op::Subs, 2); return d;
            case 9: if (b1 & 8) return fail(); d.rd = b1 & 7; d.imm = 4; set(Op::Subs, 2); return d;
            case 5: d.rd = b1 & 15; d.x = 2; d.imm = 1; set(Op::Dec, 2); return d;
            case 13: d.rd = b1 & 15; d.x = 2; d.imm = 2; set(Op::Dec, 2); return d;
            case 7: if (b1 & 8) return fail(); d.rd = b1 & 15; d.x = 4; d.imm = 1; set(Op::Dec, 2); return d;
            case 15: if (b1 & 8) return fail(); d.rd = b1 & 15; d.x = 4; d.imm = 2; set(Op::Dec, 2); return d;
            default: return fail();
          }
        case 12: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 1; set(Op::Cmp, 2); return d;
        case 13: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 2; set(Op::Cmp, 2); return d;
        case 14: d.rs = b1 >> 4; d.rd = b1 & 15; set(Op::Subx, 2); return d;
        case 15:
          if (b1 & 0x80) { if (b1 & 8) return fail(); d.rs = (b1 >> 4) & 7; d.rd = b1 & 7; d.x = 4; set(Op::Cmp, 2); return d; }
          if (!(b1 & 0xF0)) { d.rd = b1 & 15; set(Op::Das, 2); return d; }
          return fail();
      }
      break;
    }
    case 2: d.rd = lo; d.x = 1; d.imm = 0xFFFF00u | b1; d.store = false; set(Op::MovAbs8, 2); return d;
    case 3: d.rd = lo; d.x = 1; d.imm = 0xFFFF00u | b1; d.store = true; set(Op::MovAbs8, 2); return d;
    case 4: d.x = lo; d.imm = u32(s8(b1)); set(Op::Bcc8, 2); return d;
    case 5:
      switch (lo) {
        case 0: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 2; set(Op::Mulxu, 2); return d;
        case 1: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 2; set(Op::Divxu, 2); return d;
        case 2: d.rs = b1 >> 4; d.rd = b1 & 15; if (d.rd & 8) return fail(); d.x = 4; set(Op::Mulxu, 2); return d;
        case 3: d.rs = b1 >> 4; d.rd = b1 & 15; if (d.rd & 8) return fail(); d.x = 4; set(Op::Divxu, 2); return d;
        case 4: if (b1 != 0x70) return fail(); set(Op::Rts, 2); return d;
        case 5: d.imm = u32(s8(b1)); set(Op::Bsr8, 2); return d;
        case 6: if (b1 != 0x70) return fail(); set(Op::Rte, 2); return d;
        case 7: if (b1 & 0xCF) return fail(); d.imm = b1 >> 4; set(Op::Trapa, 2); return d;
        case 8: if (b1 & 15) return fail(); d.x = b1 >> 4; d.imm = se16(w16(2)); set(Op::Bcc16, 4); return d;
        case 9: if (b1 & 0x8F) return fail(); d.rd = b1 >> 4; set(Op::JmpEr, 2); return d;
        case 10: d.imm = (w16(1) << 8) | fetch(3); set(Op::JmpAbs, 4); return d;
        case 11: d.imm = b1; set(Op::JmpInd8, 2); return d;
        case 12: if (b1) return fail(); d.imm = se16(w16(2)); set(Op::Bsr16, 4); return d;
        case 13: if (b1 & 0x8F) return fail(); d.rd = b1 >> 4; set(Op::JsrEr, 2); return d;
        case 14: d.imm = (w16(1) << 8) | fetch(3); set(Op::JsrAbs, 4); return d;
        default: d.imm = b1; set(Op::JsrInd8, 2); return d;
      }
    case 6:
      switch (lo) {
        case 0: case 1: case 2: case 3: {
          static constexpr BitKind kinds[4] = {BitKind::Bset, BitKind::Bnot, BitKind::Bclr, BitKind::Btst};
          d.rs = b1 >> 4; d.rd = b1 & 15; d.x = u8(u8(kinds[lo]) | 0x80);
          set(Op::BitReg, 2);
          return d;
        }
        case 4: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 2; set(Op::Or, 2); return d;
        case 5: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 2; set(Op::Xor, 2); return d;
        case 6: d.rs = b1 >> 4; d.rd = b1 & 15; d.x = 2; set(Op::And, 2); return d;
        case 7: d.rd = b1 & 15; d.x = u8(u8((b1 & 0x80) ? BitKind::Bist : BitKind::Bst) | (((b1 >> 4) & 7) << 4)); set(Op::BitReg, 2); return d;
        case 8: d.rs = (b1 >> 4) & 7; d.rd = b1 & 15; d.store = (b1 & 0x80) != 0; d.x = 1; set(Op::MovInd, 2); return d;
        case 9: d.rs = (b1 >> 4) & 7; d.rd = b1 & 15; d.store = (b1 & 0x80) != 0; d.x = 2; set(Op::MovInd, 2); return d;
        case 10: {
          const u8 t = b1 >> 4;
          switch (t) {
            case 0: case 8: d.rd = b1 & 15; d.store = (b1 & 0x80) != 0; d.x = 1; d.imm = se16(w16(2)); set(Op::MovAbs16, 4); return d;
            case 2: case 10: d.rd = b1 & 15; d.store = (b1 & 0x80) != 0; d.x = 1; d.imm = w32(2); set(Op::MovAbs24, 6); return d;
            case 4: d.rd = b1 & 15; d.imm = se16(w16(2)); set(Op::Movfpe, 4); return d;
            case 12: d.rd = b1 & 15; d.imm = se16(w16(2)); set(Op::Movtpe, 4); return d;
            case 1: case 3: {  // bit manipulation on @aa:16 / @aa:24
              unsigned pos;
              if (t == 1) { d.imm = se16(w16(2)); pos = 4; } else { d.imm = w32(2); pos = 6; }
              const u8 c = fetch(pos), dd = fetch(pos + 1);
              // b1 & 8: BSET/BNOT/BCLR/BST group (writes); else the test group
              const bool writes = (b1 & 8) != 0;
              if (!bit_op(d, c, dd, writes)) return fail();
              set(Op::BitAbs, u8(pos + 2));
              return d;
            }
            default: return fail();
          }
        }
        case 11: {
          const u8 sub = b1 >> 4;
          if (sub & 5) return fail();
          d.rd = b1 & 15; d.store = (sub & 8) != 0; d.x = 2;
          if (sub & 2) { d.imm = w32(2); set(Op::MovAbs24, 6); } else { d.imm = se16(w16(2)); set(Op::MovAbs16, 4); }
          return d;
        }
        case 12: d.rs = (b1 >> 4) & 7; d.rd = b1 & 15; d.store = (b1 & 0x80) != 0; d.x = 1; set(Op::MovPost, 2); return d;
        case 13: d.rs = (b1 >> 4) & 7; d.rd = b1 & 15; d.store = (b1 & 0x80) != 0; d.x = 2; set(Op::MovPost, 2); return d;
        case 14: d.rs = (b1 >> 4) & 7; d.rd = b1 & 15; d.store = (b1 & 0x80) != 0; d.x = 1; d.imm = se16(w16(2)); set(Op::MovDisp16, 4); return d;
        default: d.rs = (b1 >> 4) & 7; d.rd = b1 & 15; d.store = (b1 & 0x80) != 0; d.x = 2; d.imm = se16(w16(2)); set(Op::MovDisp16, 4); return d;
      }
    case 7:
      switch (lo) {
        case 0: case 1: case 2: case 3: {
          static constexpr BitKind kinds[4] = {BitKind::Bset, BitKind::Bnot, BitKind::Bclr, BitKind::Btst};
          if (b1 & 0x80) return fail();
          d.rd = b1 & 15; d.x = u8(u8(kinds[lo]) | (((b1 >> 4) & 7) << 4));
          set(Op::BitReg, 2);
          return d;
        }
        case 4: case 5: case 6: case 7: {
          static constexpr BitKind plain[4] = {BitKind::Bor, BitKind::Bxor, BitKind::Band, BitKind::Bld};
          static constexpr BitKind inv[4] = {BitKind::Bior, BitKind::Bixor, BitKind::Biand, BitKind::Bild};
          d.rd = b1 & 15; d.x = u8(u8((b1 & 0x80) ? inv[lo - 4] : plain[lo - 4]) | (((b1 >> 4) & 7) << 4));
          set(Op::BitReg, 2);
          return d;
        }
        case 8: {  // 78 e0 6A 2x / 78 e0 6A Ax: MOV.B/W @(d:24,ERs)
          if (b1 & 0x8F) return fail();
          const u8 c = fetch(2), dd = fetch(3);
          if ((c & 0xFE) != 0x6A) return fail();
          const u8 op2 = dd >> 4;
          if ((op2 & 7) != 2) return fail();
          d.rs = b1 >> 4; d.rd = dd & 15; d.store = (op2 & 8) != 0; d.x = (c & 1) ? 2 : 1; d.imm = w32(4);
          set(Op::MovDisp24, 8);
          return d;
        }
        case 9: {
          static constexpr Op ops[7] = {Op::MovImm, Op::AddImm, Op::CmpImm, Op::SubImm, Op::OrImm, Op::XorImm, Op::AndImm};
          const u8 t = b1 >> 4;
          if (t > 6) return fail();
          d.rd = b1 & 15; d.x = 2; d.imm = w16(2);
          set(ops[t], 4);
          return d;
        }
        case 10: {
          static constexpr Op ops[7] = {Op::MovImm, Op::AddImm, Op::CmpImm, Op::SubImm, Op::OrImm, Op::XorImm, Op::AndImm};
          const u8 t = b1 >> 4;
          if (t > 6) return fail();
          d.rd = b1 & 15;
          if (d.rd & 8) return fail();
          d.x = 4; d.imm = w32(2);
          set(ops[t], 6);
          return d;
        }
        case 11: {  // EEPMOV
          if (b1 == 0x5C) d.x = 1; else if (b1 == 0xD4) d.x = 2; else return fail();
          if (fetch(2) != 0x59 || fetch(3) != 0x8F) return fail();
          set(Op::Eepmov, 4);
          return d;
        }
        default: {  // 7C/7D: bit op on @ERd; 7E/7F: bit op on @aa:8
          const u8 c = fetch(2), dd = fetch(3);
          if (dd & 0x0F) return fail();
          const bool writes = (lo & 1) != 0;
          if (lo >= 14) {
            d.imm = 0xFFFF00u | b1;
            if (!bit_op(d, c, dd, writes)) return fail();
            set(Op::BitAbs, 4);
            return d;
          }
          if (b1 & 0x8F) return fail();
          d.rd = (b1 >> 4) & 7;
          if (!bit_op(d, c, dd, writes)) return fail();
          set(Op::BitInd, 4);
          return d;
        }
      }
    case 8: d.rd = lo; d.x = 1; d.imm = b1; set(Op::AddImm, 2); return d;
    case 9: d.rd = lo; d.imm = b1; set(Op::AddxImm, 2); return d;
    case 10: d.rd = lo; d.x = 1; d.imm = b1; set(Op::CmpImm, 2); return d;
    case 11: d.rd = lo; d.imm = b1; set(Op::SubxImm, 2); return d;
    case 12: d.rd = lo; d.x = 1; d.imm = b1; set(Op::OrImm, 2); return d;
    case 13: d.rd = lo; d.x = 1; d.imm = b1; set(Op::XorImm, 2); return d;
    case 14: d.rd = lo; d.x = 1; d.imm = b1; set(Op::AndImm, 2); return d;
    default: d.rd = lo; d.x = 1; d.imm = b1; set(Op::MovImm, 2); return d;
  }
  return fail();
}

}  // namespace h8300h

// H8/500 instruction decoder.
//
// Decodes raw instruction bytes into a compact DecodedInsn record.  The decoder
// is a pure function of the bytes, so its output can be cached per address by
// the CPU core.  Encodings follow the "Machine Language Coding" tables
// (Table A-1 / Table 2-1) and the operation code maps (Table A-2..A-6) of the
// H8/500 series programming manual and the H8/510 hardware manual.
//
// Instruction formats:
//   General format:  <EA byte> [EA extension 0..2 bytes] <OP byte> [OP extension]
//                    EA byte selects addressing mode + size; OP byte selects the
//                    operation and carries the second register / control register.
//                    DADD/DSUB/MOVFPE/MOVTPE insert a 0x00 prefix before the OP byte.
//   Special format:  <OP byte(s)> [EA / displacement / immediate]
//                    Branches, system control, stack ops and the short MOV/CMP forms.
#pragma once
#include <type_traits>

#include "cpu/h8500/types.hpp"

namespace h8500 {

enum class Op : u8 {
  Invalid = 0,
  // Data transfer
  Mov, MovFpe, MovTpe, Ldm, Stm, Xch, Swap,
  // Arithmetic
  Add, AddQ, Adds, Addx, Dadd, Sub, Subs, Subx, Dsub, Mulxu, Divxu, Cmp,
  Exts, Extu, Tst, Neg, Clr, Tas,
  // Shift / rotate
  Shal, Shar, Shll, Shlr, Rotl, Rotr, Rotxl, Rotxr,
  // Logic
  And, Or, Xor, Not,
  // Bit manipulation
  Bset, Bclr, Btst, Bnot,
  // System control
  Ldc, Stc, Andc, Orc, Xorc, Trapa, TrapVs, Rte, Link, Unlk, Sleep, Nop,
  // Branch
  Bcc, Jmp, Bsr, Jsr, Rts, Rtd, Scb, Pjmp, Pjsr, Prts, Prtd,
  Count
};

// Addressing mode of the EA operand (Table 1-9 / 1-10).
enum class EaMode : u8 {
  None = 0,
  Reg,      // Rn                     1010 Sz rrr
  RegInd,   // @Rn                    1101 Sz rrr
  Disp8,    // @(d:8,Rn)              1110 Sz rrr  disp
  Disp16,   // @(d:16,Rn)             1111 Sz rrr  disp(H) disp(L)
  PreDec,   // @-Rn                   1011 Sz rrr
  PostInc,  // @Rn+                   1100 Sz rrr
  Abs8,     // @aa:8                  0000 Sz 101  addr(L)        (EA = 00:BR:aa)
  Abs16,    // @aa:16                 0001 Sz 101  addr(H) addr(L)(EA = DP:aa)
  Imm,      // #xx:8 / #xx:16         0000 Sz 100  data
  // Special-format only:
  Abs24,    // @aa:24 (PJMP/PJSR)
  PcRel8,   // d:8   (Bcc/BSR/SCB)
  PcRel16,  // d:16  (Bcc/BSR)
};

inline constexpr bool ea_is_memory(EaMode m) { return m >= EaMode::RegInd && m <= EaMode::Abs16; }

// Bcc condition field (cc).
enum class Cc : u8 { T = 0, F, HI, LS, CC, CS, NE, EQ, VC, VS, PL, MI, GE, LT, GT, LE };
// SCB condition.
enum class ScbCond : u8 { F = 0, NE = 1, EQ = 2 };
// Control register number field (ccc) for LDC/STC/ANDC/ORC/XORC.
enum class Cr : u8 { SR = 0, CCR = 1, BR = 3, EP = 4, DP = 5, TP = 7 };

inline constexpr Size cr_size(u8 cr) { return cr == 0 ? Size::Word : Size::Byte; }
inline constexpr bool cr_valid(u8 cr) { return cr != 2 && cr != 6; }

namespace insn_flags {
constexpr u8 kStore = 1 << 0;        // EA is the destination (MOV Rs,<EAd>; MOVTPE; STC; MOV:S/MOV:F store)
constexpr u8 kImmSrc = 1 << 1;       // source is `imm` from the OP side (MOV #xx,<EAd>; CMP #xx,<EAd>; ADD:Q)
constexpr u8 kImm8 = 1 << 2;         // OP-side immediate is 8 bits wide (affects timing / sign extension)
constexpr u8 kShort = 1 << 3;        // short format (MOV:E/I/L/S/F, CMP:E/I, ADD:Q)
constexpr u8 kBitInReg = 1 << 4;     // bit number comes from register `reg` (BSET Rs,<EAd>) not `aux`
constexpr u8 kMaxModeOnly = 1 << 5;  // invalid in minimum mode (PJMP/PJSR/PRTS/PRTD)
constexpr u8 kPrefixed = 1 << 6;     // carries the 0x00 prefix byte (DADD/DSUB/MOVFPE/MOVTPE)
}  // namespace insn_flags

struct DecodedInsn {
  Op op = Op::Invalid;
  Size size = Size::None;      // operand size (from Sz bit); None for size-less instructions
  EaMode ea = EaMode::None;    // addressing mode of the <EA> operand
  u8 ea_reg = 0;               // register number in the EA field (Rn), or Rs for DADD/DSUB/XCH
  u8 reg = 0;                  // Rd/Rs (OP field), CR number, Bcc cc, TRAPA vector, LDM/STM list, SCB counter
  u8 aux = 0;                  // bit number for BSET/BCLR/BNOT/BTST #xx; ScbCond for SCB
  u8 length = 0;               // instruction length in bytes (1..6)
  u8 flags = 0;                // insn_flags::*
  s32 ea_ext = 0;              // EA extension: sign-extended displacement, absolute address (8/16/24-bit)
  s32 imm = 0;                 // immediate data, sign-extended (EA #xx, OP-side #xx, ADD:Q constant, RTD/LINK/PRTD)

  constexpr bool valid() const { return op != Op::Invalid; }
  constexpr bool has(u8 f) const { return (flags & f) != 0; }
};
static_assert(sizeof(DecodedInsn) == 16, "DecodedInsn should stay compact for decode caching");

namespace detail {

template <class F>
inline u16 rd16(F& f, unsigned pos) {
  return u16((unsigned(f(pos)) << 8) | unsigned(f(pos + 1)));
}

inline void set_invalid(DecodedInsn& d, unsigned len) {
  d.op = Op::Invalid;
  d.length = u8(len);
}

// General format: EA byte first.
template <class F>
inline void decode_general(F& f, DecodedInsn& d, u8 b0) {
  using namespace insn_flags;
  unsigned pos = 1;
  d.size = (b0 & 0x08) ? Size::Word : Size::Byte;
  d.ea_reg = b0 & 7;
  switch (b0 >> 4) {
    case 0xA: d.ea = EaMode::Reg; break;
    case 0xB: d.ea = EaMode::PreDec; break;
    case 0xC: d.ea = EaMode::PostInc; break;
    case 0xD: d.ea = EaMode::RegInd; break;
    case 0xE: d.ea = EaMode::Disp8; d.ea_ext = s8(f(pos)); pos += 1; break;
    case 0xF: d.ea = EaMode::Disp16; d.ea_ext = s16(rd16(f, pos)); pos += 2; break;
    case 0x0:
      d.ea_reg = 0;
      if ((b0 & 0x07) == 0x04) {  // 0x04 / 0x0C : #xx:8 / #xx:16
        d.ea = EaMode::Imm;
        if (d.size == Size::Byte) { d.imm = s8(f(pos)); pos += 1; }
        else { d.imm = s16(rd16(f, pos)); pos += 2; }
      } else {                    // 0x05 / 0x0D : @aa:8
        d.ea = EaMode::Abs8;
        d.ea_ext = f(pos); pos += 1;
      }
      break;
    default:                      // 0x15 / 0x1D : @aa:16
      d.ea_reg = 0;
      d.ea = EaMode::Abs16;
      d.ea_ext = rd16(f, pos); pos += 2;
      break;
  }
  const bool is_reg = d.ea == EaMode::Reg;
  const bool is_imm = d.ea == EaMode::Imm;
  const bool is_mem = !is_reg && !is_imm;

  const u8 op = f(pos++);
  d.reg = op & 7;
  auto fail = [&]() { set_invalid(d, pos); };

  if (op < 0x20) {
    switch (op) {
      case 0x00: {  // prefix byte: DADD/DSUB (register EA), MOVFPE/MOVTPE (memory EA)
        const u8 b = f(pos++);
        d.reg = b & 7;
        d.flags |= kPrefixed;
        switch (b & 0xF8) {
          case 0x80: if (!is_mem) return fail(); d.op = Op::MovFpe; break;
          case 0x90: if (!is_mem) return fail(); d.op = Op::MovTpe; d.flags |= kStore; break;
          case 0xA0: if (!is_reg) return fail(); d.op = Op::Dadd; break;
          case 0xB0: if (!is_reg) return fail(); d.op = Op::Dsub; break;
          default: return fail();
        }
        break;
      }
      case 0x04: case 0x05:  // CMP:G #xx,<EAd>   (0x04: 8-bit data, 0x05: 16-bit data)
      case 0x06: case 0x07:  // MOV:G #xx,<EAd>   (0x06: 8-bit data, 0x07: 16-bit data)
        if (!is_mem) return fail();
        d.op = (op < 0x06) ? Op::Cmp : Op::Mov;
        d.flags |= kImmSrc;
        if ((op & 1) == 0) {
          // 8-bit immediate; when the EA is word-sized the data is sign-extended (MOV:G note *3).
          d.imm = s8(f(pos)); pos += 1; d.flags |= kImm8;
        } else {
          d.imm = s16(rd16(f, pos)); pos += 2;
        }
        break;
      case 0x08: case 0x09: case 0x0C: case 0x0D:  // ADD:Q #±1/#±2,<EAd>
        if (is_imm) return fail();
        d.op = Op::AddQ;
        d.flags |= kShort | kImmSrc;
        d.imm = (op == 0x08) ? 1 : (op == 0x09) ? 2 : (op == 0x0C) ? -1 : -2;
        break;
      case 0x10: if (!is_reg) return fail(); d.op = Op::Swap; d.reg = d.ea_reg; break;
      case 0x11: if (!is_reg) return fail(); d.op = Op::Exts; d.reg = d.ea_reg; break;
      case 0x12: if (!is_reg) return fail(); d.op = Op::Extu; d.reg = d.ea_reg; break;
      case 0x13: if (is_imm) return fail(); d.op = Op::Clr; break;
      case 0x14: if (is_imm) return fail(); d.op = Op::Neg; break;
      case 0x15: if (is_imm) return fail(); d.op = Op::Not; break;
      case 0x16: if (is_imm) return fail(); d.op = Op::Tst; break;
      case 0x17: if (is_imm) return fail(); d.op = Op::Tas; break;
      case 0x18: if (is_imm) return fail(); d.op = Op::Shal; break;
      case 0x19: if (is_imm) return fail(); d.op = Op::Shar; break;
      case 0x1A: if (is_imm) return fail(); d.op = Op::Shll; break;
      case 0x1B: if (is_imm) return fail(); d.op = Op::Shlr; break;
      case 0x1C: if (is_imm) return fail(); d.op = Op::Rotl; break;
      case 0x1D: if (is_imm) return fail(); d.op = Op::Rotr; break;
      case 0x1E: if (is_imm) return fail(); d.op = Op::Rotxl; break;
      case 0x1F: if (is_imm) return fail(); d.op = Op::Rotxr; break;
      default: return fail();  // 0x01-0x03, 0x0A, 0x0B, 0x0E, 0x0F
    }
    d.length = u8(pos);
    return;
  }

  switch (op >> 3) {
    case 0x04: d.op = Op::Add; break;    // 0x20  ADD  <EAs>,Rd
    case 0x05: d.op = Op::Adds; break;   // 0x28  ADDS <EAs>,Rd
    case 0x06: d.op = Op::Sub; break;    // 0x30
    case 0x07: d.op = Op::Subs; break;   // 0x38
    case 0x08: d.op = Op::Or; break;     // 0x40
    case 0x09:                           // 0x48  ORC #xx,CR | BSET Rs,<EAd>
      if (is_imm) d.op = Op::Orc; else { d.op = Op::Bset; d.flags |= kBitInReg; }
      break;
    case 0x0A: d.op = Op::And; break;    // 0x50
    case 0x0B:                           // 0x58  ANDC #xx,CR | BCLR Rs,<EAd>
      if (is_imm) d.op = Op::Andc; else { d.op = Op::Bclr; d.flags |= kBitInReg; }
      break;
    case 0x0C: d.op = Op::Xor; break;    // 0x60
    case 0x0D:                           // 0x68  XORC #xx,CR | BNOT Rs,<EAd>
      if (is_imm) d.op = Op::Xorc; else { d.op = Op::Bnot; d.flags |= kBitInReg; }
      break;
    case 0x0E: d.op = Op::Cmp; break;    // 0x70  CMP <EAs>,Rd
    case 0x0F:                           // 0x78  BTST Rs,<EAd>
      if (is_imm) return fail();
      d.op = Op::Btst; d.flags |= kBitInReg;
      break;
    case 0x10: d.op = Op::Mov; break;    // 0x80  MOV <EAs>,Rd
    case 0x11: d.op = Op::Ldc; break;    // 0x88  LDC <EAs>,CR
    case 0x12:                           // 0x90  MOV Rs,<EAd> | XCH Rs,Rd
      if (is_imm) return fail();
      if (is_reg) d.op = Op::Xch; else { d.op = Op::Mov; d.flags |= kStore; }
      break;
    case 0x13:                           // 0x98  STC CR,<EAd>
      if (is_imm) return fail();
      d.op = Op::Stc; d.flags |= kStore;
      break;
    case 0x14: d.op = Op::Addx; break;   // 0xA0
    case 0x15: d.op = Op::Mulxu; break;  // 0xA8
    case 0x16: d.op = Op::Subx; break;   // 0xB0
    case 0x17: d.op = Op::Divxu; break;  // 0xB8
    case 0x18: case 0x19: if (is_imm) return fail(); d.op = Op::Bset; d.aux = op & 0x0F; break;  // 0xC0
    case 0x1A: case 0x1B: if (is_imm) return fail(); d.op = Op::Bclr; d.aux = op & 0x0F; break;  // 0xD0
    case 0x1C: case 0x1D: if (is_imm) return fail(); d.op = Op::Bnot; d.aux = op & 0x0F; break;  // 0xE0
    default:              if (is_imm) return fail(); d.op = Op::Btst; d.aux = op & 0x0F; break;  // 0xF0
  }
  d.length = u8(pos);
}

// Special format: operation code first.
template <class F>
inline void decode_special(F& f, DecodedInsn& d, u8 b0) {
  using namespace insn_flags;
  unsigned pos = 1;
  auto fail = [&]() { set_invalid(d, pos); };

  if (b0 >= 0x20) {
    const Size sz = (b0 & 0x08) ? Size::Word : Size::Byte;
    d.reg = b0 & 7;
    switch (b0 >> 4) {
      case 0x2:  // Bcc d:8
        d.op = Op::Bcc; d.reg = b0 & 0x0F; d.ea = EaMode::PcRel8;
        d.ea_ext = s8(f(pos)); pos += 1;
        break;
      case 0x3:  // Bcc d:16
        d.op = Op::Bcc; d.reg = b0 & 0x0F; d.ea = EaMode::PcRel16;
        d.ea_ext = s16(rd16(f, pos)); pos += 2;
        break;
      case 0x4:  // CMP:E #xx:8,Rd / CMP:I #xx:16,Rd
      case 0x5:  // MOV:E #xx:8,Rd / MOV:I #xx:16,Rd
        d.op = (b0 >> 4) == 0x4 ? Op::Cmp : Op::Mov;
        d.ea = EaMode::Imm; d.flags |= kShort; d.size = sz;
        if (sz == Size::Word) { d.imm = s16(rd16(f, pos)); pos += 2; }
        else { d.imm = s8(f(pos)); pos += 1; }
        break;
      case 0x6:  // MOV:L @aa:8,Rd
      case 0x7:  // MOV:S Rs,@aa:8
        d.op = Op::Mov; d.flags |= kShort; d.size = sz; d.ea = EaMode::Abs8;
        d.ea_ext = f(pos); pos += 1;
        if ((b0 >> 4) == 0x7) d.flags |= kStore;
        break;
      case 0x8:  // MOV:F @(d:8,R6),Rd
      case 0x9:  // MOV:F Rs,@(d:8,R6)
        d.op = Op::Mov; d.flags |= kShort; d.size = sz; d.ea = EaMode::Disp8; d.ea_reg = 6;
        d.ea_ext = s8(f(pos)); pos += 1;
        if ((b0 >> 4) == 0x9) d.flags |= kStore;
        break;
      default:
        return fail();
    }
    d.length = u8(pos);
    return;
  }

  switch (b0) {
    case 0x00: d.op = Op::Nop; break;
    case 0x01: case 0x06: case 0x07: {  // SCB/F, SCB/NE, SCB/EQ  Rn,disp
      const u8 b1 = f(pos++);
      if ((b1 & 0xF8) != 0xB8) return fail();
      d.op = Op::Scb; d.reg = b1 & 7;
      d.aux = u8(b0 == 0x01 ? ScbCond::F : b0 == 0x06 ? ScbCond::NE : ScbCond::EQ);
      d.ea = EaMode::PcRel8; d.ea_ext = s8(f(pos)); pos += 1;
      break;
    }
    case 0x02: d.op = Op::Ldm; d.size = Size::Word; d.reg = f(pos++); break;  // LDM @SP+,<list>
    case 0x12: d.op = Op::Stm; d.size = Size::Word; d.reg = f(pos++); break;  // STM <list>,@-SP
    case 0x03: case 0x13:  // PJSR @aa:24 / PJMP @aa:24
      d.op = (b0 == 0x03) ? Op::Pjsr : Op::Pjmp;
      d.ea = EaMode::Abs24; d.flags |= kMaxModeOnly;
      d.ea_ext = s32((u32(f(pos)) << 16) | (u32(f(pos + 1)) << 8) | u32(f(pos + 2)));
      pos += 3;
      break;
    case 0x08: {  // TRAPA #vec  (second byte 0001 vvvv)
      const u8 b1 = f(pos++);
      // NOTE: the manual only documents 0x10..0x1F as the second byte; we mask to the
      // vector field rather than raising an invalid-instruction exception for other values.
      d.op = Op::Trapa; d.reg = b1 & 0x0F;
      break;
    }
    case 0x09: d.op = Op::TrapVs; break;
    case 0x0A: d.op = Op::Rte; break;
    case 0x0E: d.op = Op::Bsr; d.ea = EaMode::PcRel8; d.ea_ext = s8(f(pos)); pos += 1; break;
    case 0x1E: d.op = Op::Bsr; d.ea = EaMode::PcRel16; d.ea_ext = s16(rd16(f, pos)); pos += 2; break;
    case 0x0F: d.op = Op::Unlk; break;
    case 0x10: d.op = Op::Jmp; d.ea = EaMode::Abs16; d.ea_ext = rd16(f, pos); pos += 2; break;
    case 0x18: d.op = Op::Jsr; d.ea = EaMode::Abs16; d.ea_ext = rd16(f, pos); pos += 2; break;
    case 0x11: {  // register-indirect jumps, PRTS/PRTD
      const u8 b1 = f(pos++);
      switch (b1) {
        case 0x14: d.op = Op::Prtd; d.flags |= kMaxModeOnly | kImm8; d.imm = s8(f(pos)); pos += 1; break;
        case 0x19: d.op = Op::Prts; d.flags |= kMaxModeOnly; break;
        case 0x1C: d.op = Op::Prtd; d.flags |= kMaxModeOnly; d.imm = s16(rd16(f, pos)); pos += 2; break;
        default: {
          if (b1 < 0xC0) return fail();
          d.ea_reg = b1 & 7;
          const bool sub = (b1 & 0x08) != 0;
          // JMP/JSR @(d:8,Rn): the displacement is SIGN-extended, as for every
          // @(d:8,Rn) operand (Table 1-10 shows "disp (sign extension)").  Note
          // that the gearmulator H8/500 core zero-extends it here; if a ROM
          // ever misbehaves around a backward JMP @(d:8,Rn) this is the place to
          // look.  d:16 is unaffected (full 16-bit displacement either way).
          switch (b1 >> 4) {
            case 0xC: d.op = sub ? Op::Pjsr : Op::Pjmp; d.ea = EaMode::RegInd; d.flags |= kMaxModeOnly; break;
            case 0xD: d.op = sub ? Op::Jsr : Op::Jmp; d.ea = EaMode::RegInd; break;
            case 0xE: d.op = sub ? Op::Jsr : Op::Jmp; d.ea = EaMode::Disp8; d.ea_ext = s8(f(pos)); pos += 1; break;
            default:  d.op = sub ? Op::Jsr : Op::Jmp; d.ea = EaMode::Disp16; d.ea_ext = s16(rd16(f, pos)); pos += 2; break;
          }
          break;
        }
      }
      break;
    }
    case 0x14: d.op = Op::Rtd; d.flags |= kImm8; d.imm = s8(f(pos)); pos += 1; break;
    case 0x1C: d.op = Op::Rtd; d.imm = s16(rd16(f, pos)); pos += 2; break;
    case 0x17: d.op = Op::Link; d.flags |= kImm8; d.imm = s8(f(pos)); pos += 1; break;
    case 0x1F: d.op = Op::Link; d.imm = s16(rd16(f, pos)); pos += 2; break;
    case 0x19: d.op = Op::Rts; break;
    case 0x1A: d.op = Op::Sleep; break;
    default: return fail();  // 0x0B, 0x16, 0x1B
  }
  d.length = u8(pos);
}

}  // namespace detail

// Decode one instruction.  `fetch(offset)` must return the instruction byte at
// `offset` (0-based) relative to the instruction start; at most 6 bytes are read.
template <class Fetch>
  requires std::is_invocable_r_v<u8, Fetch&, unsigned>
inline DecodedInsn decode(Fetch&& fetch) {
  DecodedInsn d;
  const u8 b0 = fetch(0);
  // General-format EA bytes: 0xA0-0xFF (register modes), 0x04/0x0C (#xx),
  // 0x05/0x0D (@aa:8), 0x15/0x1D (@aa:16).  Everything else is special format.
  const bool general = (b0 >= 0xA0) || (b0 & 0xE7) == 0x05 || (b0 & 0xF7) == 0x04;
  if (general) detail::decode_general(fetch, d, b0);
  else detail::decode_special(fetch, d, b0);
  return d;
}

// Convenience overload for decoding from a byte buffer.
inline DecodedInsn decode(const u8* bytes) {
  return decode([bytes](unsigned off) { return bytes[off]; });
}

const char* op_name(Op op);
const char* cc_name(Cc cc);
const char* cr_name(u8 cr);

}  // namespace h8500

#include "cpu/h8500/timing.hpp"

namespace h8500 {
namespace {

// One row of table A-7 for a general-format instruction.  JK holds K, the
// number of OP-side bytes (see kJ below).  Columns:
//   0 Rn  1 @Rn  2 @(d:8,Rn)  3 @(d:16,Rn)  4 @-Rn  5 @Rn+  6 @aa:8  7 @aa:16  8 #xx:8  9 #xx:16
// A zero entry means the addressing mode is not available.
struct Row {
  u8 I, JK;
  u8 c[10];
};

constexpr Row kAluB     {1, 1, {2, 5, 5, 6, 5, 6, 5, 6, 3, 0}};   // ADD/SUB/CMP/AND/OR/XOR/MOV/ADDX/SUBX .B
constexpr Row kAluW     {2, 1, {2, 5, 5, 6, 5, 6, 5, 6, 0, 4}};
constexpr Row kAddsB    {1, 1, {3, 5, 5, 6, 5, 6, 5, 6, 3, 0}};   // ADDS/SUBS
constexpr Row kAddsW    {2, 1, {3, 5, 5, 6, 5, 6, 5, 6, 0, 4}};
constexpr Row kRmwB     {2, 1, {2, 7, 7, 8, 7, 8, 7, 8, 0, 0}};   // ADD:Q, NEG, NOT, shifts, rotates
constexpr Row kRmwW     {4, 1, {2, 7, 7, 8, 7, 8, 7, 8, 0, 0}};
constexpr Row kBitModB  {2, 1, {4, 7, 7, 8, 7, 8, 7, 8, 0, 0}};   // BSET/BCLR/BNOT
constexpr Row kBitModW  {4, 1, {4, 7, 7, 8, 7, 8, 7, 8, 0, 0}};
constexpr Row kBtstB    {1, 1, {3, 5, 5, 6, 5, 6, 5, 6, 0, 0}};
constexpr Row kBtstW    {2, 1, {3, 5, 5, 6, 5, 6, 5, 6, 0, 0}};
constexpr Row kClrB     {1, 1, {2, 5, 5, 6, 5, 6, 5, 6, 0, 0}};   // CLR, TST
constexpr Row kClrW     {2, 1, {2, 5, 5, 6, 5, 6, 5, 6, 0, 0}};
constexpr Row kCmpImm8  {1, 2, {0, 6, 6, 7, 6, 7, 6, 7, 0, 0}};   // CMP:G #xx:8,<EAd>
constexpr Row kCmpImm16 {2, 3, {0, 7, 7, 8, 7, 8, 7, 8, 0, 0}};   // CMP:G #xx:16,<EAd>
constexpr Row kMovImm8  {1, 2, {0, 7, 7, 8, 7, 8, 7, 8, 0, 0}};   // MOV:G #xx:8,<EAd>
constexpr Row kMovImm16 {2, 3, {0, 8, 8, 9, 8, 9, 8, 9, 0, 0}};   // MOV:G #xx:16,<EAd>
constexpr Row kDivB     {1, 1, {20, 23, 23, 24, 23, 24, 23, 24, 21, 0}};
constexpr Row kDivW     {2, 1, {26, 29, 29, 30, 29, 30, 29, 30, 0, 28}};
// DIVXU zero divide.  I differs between register/immediate and memory operands;
// see base_timing().  The manual prints 21/27 in the immediate columns of the
// maximum-mode rows, which look like copy errors (minimum-mode values); we use
// the +5 pattern of the other columns.
constexpr Row kDivZeroBMin {6, 1, {20, 23, 23, 24, 23, 24, 23, 24, 21, 0}};
constexpr Row kDivZeroBMax {10, 1, {25, 28, 28, 29, 28, 29, 28, 29, 26, 0}};
constexpr Row kDivZeroWMin {6, 1, {20, 23, 23, 24, 23, 24, 23, 24, 0, 27}};
constexpr Row kDivZeroWMax {10, 1, {25, 28, 28, 29, 28, 29, 28, 29, 0, 32}};
constexpr Row kDivOvfB  {1, 1, {8, 11, 11, 12, 11, 12, 11, 12, 9, 0}};
constexpr Row kDivOvfW  {2, 1, {8, 11, 11, 12, 11, 12, 11, 12, 0, 10}};
constexpr Row kLdcB     {1, 1, {3, 6, 6, 7, 6, 7, 6, 7, 4, 0}};
constexpr Row kLdcW     {2, 1, {4, 7, 7, 8, 7, 8, 7, 8, 0, 6}};
constexpr Row kMovPe    {0, 2, {0, 13, 13, 14, 13, 13, 13, 14, 0, 0}};  // minimum; E-clock sync adds 0..7
constexpr Row kMulB     {1, 1, {16, 19, 19, 20, 19, 20, 19, 20, 18, 0}};
constexpr Row kMulW     {2, 1, {23, 25, 25, 26, 25, 26, 25, 26, 0, 25}};
constexpr Row kStcB     {1, 1, {4, 7, 7, 8, 7, 8, 7, 8, 0, 0}};
constexpr Row kStcW     {2, 1, {4, 7, 7, 8, 7, 8, 7, 8, 0, 0}};
constexpr Row kTas      {2, 1, {4, 7, 7, 8, 7, 8, 7, 8, 0, 0}};
constexpr Row kCtlImm   {0, 1, {0, 0, 0, 0, 0, 0, 0, 0, 5, 9}};   // ANDC/ORC/XORC
// Short formats
constexpr Row kCmpE     {0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 2, 0}};
constexpr Row kCmpI     {0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 3}};
constexpr Row kMovE     {0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 2, 0}};
constexpr Row kMovI     {0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 3}};
constexpr Row kMovLSB   {1, 0, {0, 0, 0, 0, 0, 0, 5, 0, 0, 0}};
constexpr Row kMovLSW   {2, 0, {0, 0, 0, 0, 0, 0, 5, 0, 0, 0}};
constexpr Row kMovFB    {1, 0, {0, 0, 5, 0, 0, 0, 0, 0, 0, 0}};
constexpr Row kMovFW    {2, 0, {0, 0, 5, 0, 0, 0, 0, 0, 0, 0}};
// Register-only
constexpr Row kDaddDsub {0, 2, {4, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
constexpr Row kExt      {0, 1, {3, 0, 0, 0, 0, 0, 0, 0, 0, 0}};   // EXTS/EXTU/SWAP
constexpr Row kXch      {0, 1, {4, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

int ea_column(const DecodedInsn& d) {
  switch (d.ea) {
    case EaMode::Reg: return 0;
    case EaMode::RegInd: return 1;
    case EaMode::Disp8: return 2;
    case EaMode::Disp16: return 3;
    case EaMode::PreDec: return 4;
    case EaMode::PostInc: return 5;
    case EaMode::Abs8: return 6;
    case EaMode::Abs16: return 7;
    case EaMode::Imm: return d.size == Size::Word ? 9 : 8;
    default: return -1;
  }
}

// J: instruction bytes occupied by the EA field, per addressing mode (the row of
// numbers under the mode headers of table A-7).  K (Row::JK) is the number of
// OP-side bytes.  J + K is the number of bytes fetched for the instruction.
constexpr u8 kJ[10] = {1, 1, 2, 3, 1, 1, 2, 3, 2, 3};

BaseTiming from_row(const Row& r, const DecodedInsn& d) {
  const int col = ea_column(d);
  BaseTiming t;
  t.I = r.I;
  t.JK = u8(r.JK + (col >= 0 ? kJ[col] : 0));
  t.states = col >= 0 ? r.c[col] : 0;
  return t;
}

BaseTiming bt(u8 states, u8 I, u8 JK) { return BaseTiming{states, I, JK}; }

}  // namespace

BaseTiming base_timing(const DecodedInsn& d, const TimingContext& ctx) {
  using namespace insn_flags;
  const bool W = d.size == Size::Word;
  switch (d.op) {
    // ---- general format --------------------------------------------------
    case Op::Mov:
      if (d.has(kShort)) {
        if (d.ea == EaMode::Imm) return from_row(W ? kMovI : kMovE, d);
        if (d.ea == EaMode::Abs8) return from_row(W ? kMovLSW : kMovLSB, d);
        return from_row(W ? kMovFW : kMovFB, d);
      }
      if (d.has(kImmSrc)) return from_row(d.has(kImm8) ? kMovImm8 : kMovImm16, d);
      return from_row(W ? kAluW : kAluB, d);
    case Op::Cmp:
      if (d.has(kShort)) return from_row(W ? kCmpI : kCmpE, d);
      if (d.has(kImmSrc)) return from_row(d.has(kImm8) ? kCmpImm8 : kCmpImm16, d);
      return from_row(W ? kAluW : kAluB, d);
    case Op::Add: case Op::Sub: case Op::And: case Op::Or: case Op::Xor: case Op::Addx: case Op::Subx:
      return from_row(W ? kAluW : kAluB, d);
    case Op::Adds: case Op::Subs:
      return from_row(W ? kAddsW : kAddsB, d);
    case Op::AddQ: case Op::Neg: case Op::Not:
    case Op::Shal: case Op::Shar: case Op::Shll: case Op::Shlr:
    case Op::Rotl: case Op::Rotr: case Op::Rotxl: case Op::Rotxr:
      return from_row(W ? kRmwW : kRmwB, d);
    case Op::Bset: case Op::Bclr: case Op::Bnot:
      return from_row(W ? kBitModW : kBitModB, d);
    case Op::Btst:
      return from_row(W ? kBtstW : kBtstB, d);
    case Op::Clr: case Op::Tst:
      return from_row(W ? kClrW : kClrB, d);
    case Op::Tas:
      return from_row(kTas, d);
    case Op::Mulxu:
      return from_row(W ? kMulW : kMulB, d);
    case Op::Divxu: {
      if (ctx.cond == ExecCond::DivOverflow) return from_row(W ? kDivOvfW : kDivOvfB, d);
      if (ctx.cond == ExecCond::DivZero) {
        const Row& r = W ? (ctx.max_mode ? kDivZeroWMax : kDivZeroWMin)
                         : (ctx.max_mode ? kDivZeroBMax : kDivZeroBMin);
        BaseTiming t = from_row(r, d);
        if (ea_is_memory(d.ea)) t.I = u8(t.I + (W ? 2 : 1));  // memory operand: 7/11 (.B), 8/12 (.W)
        return t;
      }
      return from_row(W ? kDivW : kDivB, d);
    }
    case Op::Ldc: return from_row(W ? kLdcW : kLdcB, d);
    case Op::Stc: return from_row(W ? kStcW : kStcB, d);
    case Op::Andc: case Op::Orc: case Op::Xorc: return from_row(kCtlImm, d);
    case Op::MovFpe: case Op::MovTpe: return from_row(kMovPe, d);
    case Op::Dadd: case Op::Dsub: return from_row(kDaddDsub, d);
    case Op::Exts: case Op::Extu: case Op::Swap: return from_row(kExt, d);
    case Op::Xch: return from_row(kXch, d);

    // ---- special format --------------------------------------------------
    case Op::Bcc:
      if (d.ea == EaMode::PcRel8) return ctx.cond == ExecCond::BranchTaken ? bt(7, 0, 5) : bt(3, 0, 2);
      return ctx.cond == ExecCond::BranchTaken ? bt(7, 0, 6) : bt(3, 0, 3);
    case Op::Bsr:
      return d.ea == EaMode::PcRel8 ? bt(9, 2, 4) : bt(9, 2, 5);
    case Op::Jmp:
      switch (d.ea) {
        case EaMode::Abs16: return bt(7, 0, 5);
        case EaMode::RegInd: return bt(6, 0, 5);
        case EaMode::Disp8: return bt(7, 0, 5);
        default: return bt(8, 0, 6);
      }
    case Op::Jsr:
      switch (d.ea) {
        case EaMode::Abs16: return bt(9, 2, 5);
        case EaMode::RegInd: return bt(9, 2, 5);
        case EaMode::Disp8: return bt(9, 2, 5);
        default: return bt(10, 2, 6);
      }
    case Op::Ldm: return bt(u8(6 + 4 * ctx.n_regs), u8(2 * ctx.n_regs), 2);
    case Op::Stm: return bt(u8(6 + 3 * ctx.n_regs), u8(2 * ctx.n_regs), 2);
    case Op::Link: return d.has(kImm8) ? bt(6, 2, 2) : bt(7, 2, 3);
    case Op::Nop: return bt(2, 0, 1);
    case Op::Rtd: return d.has(kImm8) ? bt(9, 2, 4) : bt(9, 2, 5);
    case Op::Rte: return ctx.max_mode ? bt(15, 6, 4) : bt(13, 4, 4);
    case Op::Rts: return bt(8, 2, 4);
    case Op::Scb:
      switch (ctx.cond) {
        case ExecCond::BranchTaken: return bt(8, 0, 6);
        case ExecCond::ScbMinus1: return bt(4, 0, 3);
        default: return bt(3, 0, 3);
      }
    case Op::Sleep: return bt(2, 0, 0);
    case Op::Trapa: return ctx.max_mode ? bt(22, 10, 4) : bt(17, 6, 4);
    case Op::TrapVs:
      if (ctx.cond != ExecCond::BranchTaken) return bt(3, 0, 1);
      return ctx.max_mode ? bt(23, 10, 4) : bt(18, 6, 4);
    case Op::Unlk: return bt(5, 2, 1);
    case Op::Pjmp: return d.ea == EaMode::Abs24 ? bt(9, 0, 6) : bt(8, 0, 5);
    case Op::Pjsr: return d.ea == EaMode::Abs24 ? bt(15, 4, 6) : bt(13, 4, 5);
    case Op::Prts: return bt(12, 4, 5);
    case Op::Prtd: return d.has(kImm8) ? bt(13, 4, 5) : bt(13, 4, 6);
    case Op::Invalid:
    case Op::Count:
      break;
  }
  return bt(0, 0, 0);
}

unsigned parity_adjustment(const DecodedInsn& d, bool odd_start, ExecCond cond) {
  using namespace insn_flags;
  switch (d.op) {
    case Op::Bsr: case Op::Jmp: case Op::Jsr: case Op::Rts: case Op::Rtd: case Op::Rte:
    case Op::Trapa: case Op::Pjmp: case Op::Pjsr: case Op::Prts: case Op::Prtd:
      return odd_start ? 1 : 0;
    case Op::Bcc: case Op::Scb: case Op::TrapVs:
      return (cond == ExecCond::BranchTaken && odd_start) ? 1 : 0;
    case Op::MovFpe: case Op::MovTpe:
      return 1;
    case Op::Mov:
      if (d.has(kImmSrc) && !d.has(kShort)) {
        if (d.has(kImm8)) return 1;  // MOV.B #xx:8,<EA>: 1 for every mode and parity
        // MOV.W #xx:16,<EA>
        static constexpr u8 kEven[8] = {0, 2, 0, 2, 2, 2, 0, 2};
        static constexpr u8 kOdd[8] = {0, 0, 2, 0, 0, 0, 2, 0};
        const int col = ea_column(d);
        if (col < 0 || col > 7) return 0;
        return odd_start ? kOdd[col] : kEven[col];
      }
      [[fallthrough]];
    default: {
      // "Instructions other than above", by addressing mode.
      static constexpr u8 kEven[10] = {0, 1, 0, 1, 1, 1, 0, 1, 0, 0};
      static constexpr u8 kOdd[10] = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
      const int col = ea_column(d);
      if (col < 0) return 0;
      return odd_start ? kOdd[col] : kEven[col];
    }
  }
}

}  // namespace h8500

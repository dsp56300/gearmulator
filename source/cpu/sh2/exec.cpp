// SH-2 instruction handlers and cell fill.
//
// One template `h<Op, Var>` implements every instruction; the variant says how
// the cell is executed.  Plain executes and chains to the next cell.  Line
// first charges the external fetch of the 4-byte line through the instruction
// cache model.  Slot executes the instruction as the delay slot of the branch
// that owns the cell (operands from the slot fields) and chains to the
// branch's completion.  Bad / BadLine are delayed branches whose slot holds a
// slot-illegal instruction.  Semantics follow the SH-1/SH-2 programming manual.
#include <algorithm>
#include <utility>

#include "cpu/sh2/cpu.hpp"

#include <cstdio>
#include <cstdlib>

namespace sh2 {

#if defined(__clang__) && __has_cpp_attribute(clang::musttail)
#define SH2_HAVE_MUSTTAIL 1
#define SH2_CHAIN(nx) [[clang::musttail]] return (nx)->fn(cpu, (nx))
#define SH2_CHAIN_TO(handler, cell) [[clang::musttail]] return (handler)(cpu, (cell))
#else
#define SH2_HAVE_MUSTTAIL 0
#define SH2_CHAIN(nx) return (nx)
#define SH2_CHAIN_TO(handler, cell) return (handler)(cpu, (cell))
#endif

// Finish an instruction: charge its states, then either continue with the
// branch completion (delay slot) or with the following cell.
#define SH2_END(states)                                        \
  do {                                                         \
    ++cpu.insn_count_;                                         \
    if constexpr (kSlot) {                                     \
      cpu.budget_ -= s32(states);                              \
      SH2_CHAIN_TO(cpu.branch_finish_, c);                     \
    } else {                                                   \
      cpu.budget_ -= s32(states) + fetch;                      \
      const Cell* nx_ = c + 1;                                 \
      if (SH2_UNLIKELY(cpu.budget_ <= 0)) return nx_;          \
      SH2_CHAIN(nx_);                                          \
    }                                                          \
  } while (0)
// Finish a taken branch / exception: continue at `target`.
#define SH2_JUMP(target, states)                               \
  do {                                                         \
    cpu.budget_ -= s32(states) + fetch;                        \
    ++cpu.insn_count_;                                         \
    const Cell* nx_ = (target);                                \
    if (SH2_UNLIKELY(cpu.budget_ <= 0)) return nx_;            \
    SH2_CHAIN(nx_);                                            \
  } while (0)

enum class Var : u8 { Plain, Line, Slot, Bad, BadLine };

struct ExecImpl {
  static constexpr u32 kT = Cpu::kT, kS = Cpu::kS, kQ = Cpu::kQ, kM = Cpu::kM;

  static u32 sx8(u32 v) { return u32(s32(s8(v))); }
  static u32 sx16(u32 v) { return u32(s32(s16(v))); }
  static void set_t(Cpu& cpu, bool t) { cpu.regs_.sr = (cpu.regs_.sr & ~kT) | (t ? kT : 0); }
  static bool t(const Cpu& cpu) { return cpu.regs_.sr & kT; }

  // ---- delayed branches ---------------------------------------------------------
  // The prelude decides the target and completion before the slot runs (the
  // slot may overwrite the registers the branch used), the slot chains to the
  // completion, which charges the branch and continues at the target.
  template <unsigned kStates>
  static const Cell* finish_direct(Cpu& cpu, const Cell*) {
    const u8 fetch = cpu.branch_fetch_;
    SH2_JUMP(cpu.branch_cell_, kStates);
  }
  template <unsigned kStates>
  static const Cell* finish_indirect(Cpu& cpu, const Cell*) {
    const u8 fetch = cpu.branch_fetch_;
    SH2_JUMP(cpu.branch_to(cpu.branch_target_), kStates);
  }
  static const Cell* finish_rte(Cpu& cpu, const Cell*) {
    const u8 fetch = cpu.branch_fetch_;
    cpu.set_sr(cpu.branch_sr_);
    SH2_JUMP(cpu.branch_to(cpu.branch_target_), 4);
  }
  // PC-relative target of the branch at `pc` (imm is a cell delta once the
  // fill resolved the target inside the page).
  static u32 relative_pc(const Cell* c, u32 pc) {
    return (c->flags & kFlagDirect) ? pc + u32(c->imm) * 2 : pc + 4 + u32(c->imm);
  }
  // The target as a cell: direct when resolved, otherwise through branch_to().
  static const Cell* relative_target(Cpu& cpu, const Cell* c, u32 pc) {
    if (c->flags & kFlagDirect) {
      if (SH2_UNLIKELY(c->flags & kFlagTargetLine)) cpu.fetch_line_at(pc + u32(c->imm) * 2);
      return c + c->imm;
    }
    return cpu.branch_to(pc + 4 + u32(c->imm));
  }
  static void arm_relative(Cpu& cpu, const Cell* c, u32 pc) {
    if (c->flags & kFlagDirect) {
      if (SH2_UNLIKELY(c->flags & kFlagTargetLine)) cpu.fetch_line_at(pc + u32(c->imm) * 2);
      cpu.branch_cell_ = c + c->imm;
      cpu.branch_finish_ = &finish_direct<2>;
    } else {
      cpu.branch_target_ = pc + 4 + u32(c->imm);
      cpu.branch_finish_ = &finish_indirect<2>;
    }
  }
  static void arm_fallthrough(Cpu& cpu, const Cell* c, u32 pc) {
    if (SH2_UNLIKELY(c->flags & kFlagPageEdge)) {
      cpu.branch_target_ = pc + 4;
      cpu.branch_finish_ = &finish_indirect<1>;
    } else {
      cpu.branch_cell_ = c + 2;
      cpu.branch_finish_ = &finish_direct<1>;
    }
  }
  static void arm_absolute(Cpu& cpu, u32 target) {
    cpu.branch_target_ = target;
    cpu.branch_finish_ = &finish_indirect<2>;
  }

  // MAC.W / MAC.L accumulation with the S-bit saturation rules.
  static void mac_add(Cpu& cpu, s64 product, bool word) {
    auto& r = cpu.regs_;
    if (!(r.sr & kS)) {
      const u64 acc = (u64(r.mach) << 32) | r.macl;
      const u64 sum = acc + u64(product);
      r.mach = u32(sum >> 32);
      r.macl = u32(sum);
      return;
    }
    if (word) {
      // 32-bit saturation into MACL; MACH bit 0 flags the overflow.
      const s64 sum = s64(s32(r.macl)) + product;
      if (sum > 0x7FFFFFFFLL) { r.macl = 0x7FFFFFFFu; r.mach |= 1; }
      else if (sum < -0x80000000LL) { r.macl = 0x80000000u; r.mach |= 1; }
      else r.macl = u32(sum);
      return;
    }
    // 48-bit saturation, as the manual's MAC.L pseudo-code does it: the low
    // 16 bits of MACH join the accumulation, the 32-bit signed result decides
    // the overflow, and the saturated MACH is H'00007FFF / H'00008000.
    const u64 sum = u64(r.macl) + u64(u32(product));
    u32 hi = u32(u64(product) >> 32) + u32(sum >> 32) + (r.mach & 0xFFFF);
    u32 lo = u32(sum);
    if (s32(hi) < 0 && hi < 0xFFFF8000u) { hi = 0x00008000u; lo = 0; }
    else if (s32(hi) > 0 && hi > 0x00007FFFu) { hi = 0x00007FFFu; lo = 0xFFFFFFFFu; }
    r.mach = hi;
    r.macl = lo;
  }

  // DIV1 single-step division, literally the manual's algorithm.
  static void div1(Cpu& cpu, unsigned n, unsigned m) {
    u32& sr = cpu.regs_.sr;
    u32& rn = cpu.regs_.r[n];
    const bool old_q = (sr & kQ) != 0, mbit = (sr & kM) != 0;
    bool q = (rn & 0x80000000u) != 0;
    rn = (rn << 1) | (sr & kT);
    const u32 rm = cpu.regs_.r[m];  // after the shift: DIV1 Rn,Rn divides by the shifted value
    const u32 tmp0 = rn;
    bool tmp1;
    if (old_q == mbit) {  // subtract
      rn -= rm;
      tmp1 = rn > tmp0;
    } else {              // add
      rn += rm;
      tmp1 = rn < tmp0;
    }
    // Q0 M0: Q = q ? !tmp1 : tmp1        Q0 M1: Q = q ? tmp1 : !tmp1
    // Q1 M0: Q = q ? !tmp1 : tmp1        Q1 M1: Q = q ? tmp1 : !tmp1
    q = (mbit == q) ? tmp1 : !tmp1;
    sr = (sr & ~(kQ | kT)) | (q ? kQ : 0) | ((q == mbit) ? kT : 0);
  }

  template <Op O, Var V>
  static const Cell* h(Cpu& cpu, const Cell* c) {
    constexpr bool kSlot = V == Var::Slot;
    constexpr bool kLine = V == Var::Line || V == Var::BadLine;
    constexpr bool kBad = V == Var::Bad || V == Var::BadLine;
    static_assert(!kSlot || !writes_pc(O));
    static_assert(!kBad || is_delayed_branch(O));
    // Everything the cell provides is read up front: the instruction may
    // drop the decoded cells (a store into code, a bus retiming).
    const u32 pc = cpu.pc_of(c) + (kSlot ? 2 : 0);
    u32* const r = cpu.regs_.r;
    u32& sr = cpu.regs_.sr;
    const unsigned n = kSlot ? c->slot_n : c->n, m = kSlot ? c->slot_m : c->m;
    const s32 imm = kSlot ? c->slot_imm : c->imm;
    const u32 uimm = u32(imm);
    [[maybe_unused]] const u8 cyc = kSlot ? c->slot_cyc : c->cyc;
    [[maybe_unused]] const u8 fetch = c->fetch;
    [[maybe_unused]] const Handler slot = c->real;
    if constexpr (kLine) {
      // A fused delayed branch owns exactly one line start: its own word or its slot's.
      cpu.fetch_line_cached(is_delayed_branch(O) && !kBad ? (pc + 2) & ~3u : pc, u8(c->flags & Bus::kClassMask));
    }

    // ---- data transfer --------------------------------------------------------
    if constexpr (O == Op::MovI) { r[n] = uimm; SH2_END(1); }
    else if constexpr (O == Op::MovwPc) { r[n] = sx16(cpu.read16(pc + 4 + uimm)); SH2_END(1); }
    else if constexpr (O == Op::MovlPc) { r[n] = cpu.read32(((pc + 4) & ~3u) + uimm); SH2_END(1); }
    else if constexpr (O == Op::MovR) { r[n] = r[m]; SH2_END(1); }
    else if constexpr (O == Op::MovbS) { cpu.write8(r[n], r[m]); SH2_END(1); }
    else if constexpr (O == Op::MovwS) { cpu.write16(r[n], r[m]); SH2_END(1); }
    else if constexpr (O == Op::MovlS) { cpu.write32(r[n], r[m]); SH2_END(1); }
    else if constexpr (O == Op::MovbL) { r[n] = sx8(cpu.read8(r[m])); SH2_END(1); }
    else if constexpr (O == Op::MovwL) { r[n] = sx16(cpu.read16(r[m])); SH2_END(1); }
    else if constexpr (O == Op::MovlL) { r[n] = cpu.read32(r[m]); SH2_END(1); }
    else if constexpr (O == Op::MovbM) { const u32 v = r[m]; r[n] -= 1; cpu.write8(r[n], v); SH2_END(1); }
    else if constexpr (O == Op::MovwM) { const u32 v = r[m]; r[n] -= 2; cpu.write16(r[n], v); SH2_END(1); }
    else if constexpr (O == Op::MovlM) { const u32 v = r[m]; r[n] -= 4; cpu.write32(r[n], v); SH2_END(1); }
    else if constexpr (O == Op::MovbP) { const u32 v = sx8(cpu.read8(r[m])); if (n != m) r[m] += 1; r[n] = v; SH2_END(1); }
    else if constexpr (O == Op::MovwP) { const u32 v = sx16(cpu.read16(r[m])); if (n != m) r[m] += 2; r[n] = v; SH2_END(1); }
    else if constexpr (O == Op::MovlP) { const u32 v = cpu.read32(r[m]); if (n != m) r[m] += 4; r[n] = v; SH2_END(1); }
    else if constexpr (O == Op::MovbS4) { cpu.write8(r[n] + uimm, r[0]); SH2_END(1); }
    else if constexpr (O == Op::MovwS4) { cpu.write16(r[n] + uimm, r[0]); SH2_END(1); }
    else if constexpr (O == Op::MovlS4) { cpu.write32(r[n] + uimm, r[m]); SH2_END(1); }
    else if constexpr (O == Op::MovbL4) { r[0] = sx8(cpu.read8(r[m] + uimm)); SH2_END(1); }
    else if constexpr (O == Op::MovwL4) { r[0] = sx16(cpu.read16(r[m] + uimm)); SH2_END(1); }
    else if constexpr (O == Op::MovlL4) { r[n] = cpu.read32(r[m] + uimm); SH2_END(1); }
    else if constexpr (O == Op::MovbS0) { cpu.write8(r[0] + r[n], r[m]); SH2_END(1); }
    else if constexpr (O == Op::MovwS0) { cpu.write16(r[0] + r[n], r[m]); SH2_END(1); }
    else if constexpr (O == Op::MovlS0) { cpu.write32(r[0] + r[n], r[m]); SH2_END(1); }
    else if constexpr (O == Op::MovbL0) { r[n] = sx8(cpu.read8(r[0] + r[m])); SH2_END(1); }
    else if constexpr (O == Op::MovwL0) { r[n] = sx16(cpu.read16(r[0] + r[m])); SH2_END(1); }
    else if constexpr (O == Op::MovlL0) { r[n] = cpu.read32(r[0] + r[m]); SH2_END(1); }
    else if constexpr (O == Op::MovbSG) { cpu.write8(cpu.regs_.gbr + uimm, r[0]); SH2_END(1); }
    else if constexpr (O == Op::MovwSG) { cpu.write16(cpu.regs_.gbr + uimm, r[0]); SH2_END(1); }
    else if constexpr (O == Op::MovlSG) { cpu.write32(cpu.regs_.gbr + uimm, r[0]); SH2_END(1); }
    else if constexpr (O == Op::MovbLG) { r[0] = sx8(cpu.read8(cpu.regs_.gbr + uimm)); SH2_END(1); }
    else if constexpr (O == Op::MovwLG) { r[0] = sx16(cpu.read16(cpu.regs_.gbr + uimm)); SH2_END(1); }
    else if constexpr (O == Op::MovlLG) { r[0] = cpu.read32(cpu.regs_.gbr + uimm); SH2_END(1); }
    else if constexpr (O == Op::Mova) { r[0] = ((pc + 4) & ~3u) + uimm; SH2_END(1); }
    else if constexpr (O == Op::Movt) { r[n] = sr & kT; SH2_END(1); }
    else if constexpr (O == Op::SwapB) { const u32 v = r[m]; r[n] = (v & 0xFFFF0000u) | ((v & 0xFF) << 8) | ((v >> 8) & 0xFF); SH2_END(1); }
    else if constexpr (O == Op::SwapW) { const u32 v = r[m]; r[n] = (v << 16) | (v >> 16); SH2_END(1); }
    else if constexpr (O == Op::Xtrct) { r[n] = (r[m] << 16) | (r[n] >> 16); SH2_END(1); }

    // ---- arithmetic -----------------------------------------------------------
    else if constexpr (O == Op::Add) { r[n] += r[m]; SH2_END(1); }
    else if constexpr (O == Op::AddI) { r[n] += uimm; SH2_END(1); }
    else if constexpr (O == Op::AddC) {
      const u32 tmp1 = r[n] + r[m], tmp0 = r[n];
      r[n] = tmp1 + (sr & kT);
      set_t(cpu, tmp0 > tmp1 || tmp1 > r[n]);
      SH2_END(1);
    }
    else if constexpr (O == Op::AddV) {
      const s32 d = s32(r[n]), s = s32(r[m]);
      const s32 ans = s32(u32(d) + u32(s));
      set_t(cpu, ((d >= 0) == (s >= 0)) && ((ans >= 0) != (d >= 0)));
      r[n] = u32(ans);
      SH2_END(1);
    }
    else if constexpr (O == Op::CmpEqI) { set_t(cpu, r[0] == uimm); SH2_END(1); }
    else if constexpr (O == Op::CmpEq) { set_t(cpu, r[n] == r[m]); SH2_END(1); }
    else if constexpr (O == Op::CmpHs) { set_t(cpu, r[n] >= r[m]); SH2_END(1); }
    else if constexpr (O == Op::CmpGe) { set_t(cpu, s32(r[n]) >= s32(r[m])); SH2_END(1); }
    else if constexpr (O == Op::CmpHi) { set_t(cpu, r[n] > r[m]); SH2_END(1); }
    else if constexpr (O == Op::CmpGt) { set_t(cpu, s32(r[n]) > s32(r[m])); SH2_END(1); }
    else if constexpr (O == Op::CmpPl) { set_t(cpu, s32(r[n]) > 0); SH2_END(1); }
    else if constexpr (O == Op::CmpPz) { set_t(cpu, s32(r[n]) >= 0); SH2_END(1); }
    else if constexpr (O == Op::CmpStr) {
      const u32 x = r[n] ^ r[m];
      set_t(cpu, !(x & 0xFF000000u) || !(x & 0x00FF0000u) || !(x & 0x0000FF00u) || !(x & 0x000000FFu));
      SH2_END(1);
    }
    else if constexpr (O == Op::Div1) { div1(cpu, n, m); SH2_END(1); }
    else if constexpr (O == Op::Div0S) {
      const u32 q = r[n] >> 31, mm = r[m] >> 31;
      sr = (sr & ~(kQ | kM | kT)) | (q ? kQ : 0) | (mm ? kM : 0) | ((q ^ mm) ? kT : 0);
      SH2_END(1);
    }
    else if constexpr (O == Op::Div0U) { sr &= ~(kQ | kM | kT); SH2_END(1); }
    else if constexpr (O == Op::DmulsL) {
      const s64 p = s64(s32(r[n])) * s64(s32(r[m]));
      cpu.regs_.mach = u32(u64(p) >> 32);
      cpu.regs_.macl = u32(p);
      SH2_END(cyc);
    }
    else if constexpr (O == Op::DmuluL) {
      const u64 p = u64(r[n]) * u64(r[m]);
      cpu.regs_.mach = u32(p >> 32);
      cpu.regs_.macl = u32(p);
      SH2_END(cyc);
    }
    else if constexpr (O == Op::Dt) { r[n] -= 1; set_t(cpu, r[n] == 0); SH2_END(1); }
    else if constexpr (O == Op::ExtsB) { r[n] = sx8(r[m]); SH2_END(1); }
    else if constexpr (O == Op::ExtsW) { r[n] = sx16(r[m]); SH2_END(1); }
    else if constexpr (O == Op::ExtuB) { r[n] = r[m] & 0xFF; SH2_END(1); }
    else if constexpr (O == Op::ExtuW) { r[n] = r[m] & 0xFFFF; SH2_END(1); }
    else if constexpr (O == Op::MacL) {
      const s32 a = s32(cpu.read32(r[n]));
      r[n] += 4;
      const s32 b = s32(cpu.read32(r[m]));
      r[m] += 4;
      mac_add(cpu, s64(a) * s64(b), false);
      SH2_END(cyc);
    }
    else if constexpr (O == Op::MacW) {
      const s32 a = s32(s16(cpu.read16(r[n])));
      r[n] += 2;
      const s32 b = s32(s16(cpu.read16(r[m])));
      r[m] += 2;
      mac_add(cpu, s64(a) * s64(b), true);
      SH2_END(cyc);
    }
    else if constexpr (O == Op::MulL) { cpu.regs_.macl = r[n] * r[m]; SH2_END(cyc); }
    else if constexpr (O == Op::MulsW) { cpu.regs_.macl = u32(s32(s16(r[n])) * s32(s16(r[m]))); SH2_END(1); }
    else if constexpr (O == Op::MuluW) { cpu.regs_.macl = u32(u16(r[n])) * u32(u16(r[m])); SH2_END(1); }
    else if constexpr (O == Op::Neg) { r[n] = 0u - r[m]; SH2_END(1); }
    else if constexpr (O == Op::NegC) {
      const u32 tmp = 0u - r[m];
      r[n] = tmp - (sr & kT);
      set_t(cpu, 0 < tmp || tmp < r[n]);
      SH2_END(1);
    }
    else if constexpr (O == Op::Sub) { r[n] -= r[m]; SH2_END(1); }
    else if constexpr (O == Op::SubC) {
      const u32 tmp1 = r[n] - r[m], tmp0 = r[n];
      r[n] = tmp1 - (sr & kT);
      set_t(cpu, tmp0 < tmp1 || tmp1 < r[n]);
      SH2_END(1);
    }
    else if constexpr (O == Op::SubV) {
      const s32 d = s32(r[n]), s = s32(r[m]);
      const s32 ans = s32(u32(d) - u32(s));
      set_t(cpu, ((d >= 0) != (s >= 0)) && ((ans >= 0) != (d >= 0)));
      r[n] = u32(ans);
      SH2_END(1);
    }

    // ---- logic ----------------------------------------------------------------
    else if constexpr (O == Op::And) { r[n] &= r[m]; SH2_END(1); }
    else if constexpr (O == Op::AndI) { r[0] &= uimm; SH2_END(1); }
    else if constexpr (O == Op::AndB) { const u32 a = cpu.regs_.gbr + r[0]; cpu.write8(a, cpu.read8(a) & uimm); SH2_END(3); }
    else if constexpr (O == Op::Not) { r[n] = ~r[m]; SH2_END(1); }
    else if constexpr (O == Op::Or) { r[n] |= r[m]; SH2_END(1); }
    else if constexpr (O == Op::OrI) { r[0] |= uimm; SH2_END(1); }
    else if constexpr (O == Op::OrB) { const u32 a = cpu.regs_.gbr + r[0]; cpu.write8(a, cpu.read8(a) | uimm); SH2_END(3); }
    else if constexpr (O == Op::Tas) {
      const u32 v = cpu.read8(r[n]);
      set_t(cpu, v == 0);
      cpu.write8(r[n], v | 0x80);
      SH2_END(4);
    }
    else if constexpr (O == Op::Tst) { set_t(cpu, (r[n] & r[m]) == 0); SH2_END(1); }
    else if constexpr (O == Op::TstI) { set_t(cpu, (r[0] & uimm) == 0); SH2_END(1); }
    else if constexpr (O == Op::TstB) { set_t(cpu, (cpu.read8(cpu.regs_.gbr + r[0]) & uimm) == 0); SH2_END(3); }
    else if constexpr (O == Op::Xor) { r[n] ^= r[m]; SH2_END(1); }
    else if constexpr (O == Op::XorI) { r[0] ^= uimm; SH2_END(1); }
    else if constexpr (O == Op::XorB) { const u32 a = cpu.regs_.gbr + r[0]; cpu.write8(a, cpu.read8(a) ^ uimm); SH2_END(3); }

    // ---- shifts ---------------------------------------------------------------
    else if constexpr (O == Op::Rotl) { const u32 hi = r[n] >> 31; r[n] = (r[n] << 1) | hi; set_t(cpu, hi); SH2_END(1); }
    else if constexpr (O == Op::Rotr) { const u32 lo = r[n] & 1; r[n] = (r[n] >> 1) | (lo << 31); set_t(cpu, lo); SH2_END(1); }
    else if constexpr (O == Op::Rotcl) { const u32 hi = r[n] >> 31; r[n] = (r[n] << 1) | (sr & kT); set_t(cpu, hi); SH2_END(1); }
    else if constexpr (O == Op::Rotcr) { const u32 lo = r[n] & 1; r[n] = (r[n] >> 1) | ((sr & kT) << 31); set_t(cpu, lo); SH2_END(1); }
    else if constexpr (O == Op::Shal || O == Op::Shll) { set_t(cpu, r[n] >> 31); r[n] <<= 1; SH2_END(1); }
    else if constexpr (O == Op::Shar) { set_t(cpu, r[n] & 1); r[n] = u32(s32(r[n]) >> 1); SH2_END(1); }
    else if constexpr (O == Op::Shlr) { set_t(cpu, r[n] & 1); r[n] >>= 1; SH2_END(1); }
    else if constexpr (O == Op::Shll2) { r[n] <<= 2; SH2_END(1); }
    else if constexpr (O == Op::Shlr2) { r[n] >>= 2; SH2_END(1); }
    else if constexpr (O == Op::Shll8) { r[n] <<= 8; SH2_END(1); }
    else if constexpr (O == Op::Shlr8) { r[n] >>= 8; SH2_END(1); }
    else if constexpr (O == Op::Shll16) { r[n] <<= 16; SH2_END(1); }
    else if constexpr (O == Op::Shlr16) { r[n] >>= 16; SH2_END(1); }

    // ---- branches -------------------------------------------------------------
    else if constexpr (O == Op::Bf || O == Op::Bt) {
      const bool taken = (O == Op::Bt) == t(cpu);
      if (!taken) SH2_END(1);
      SH2_JUMP(relative_target(cpu, c, pc), 3);
    }
    else if constexpr (O == Op::BfS || O == Op::BtS || O == Op::Bra || O == Op::Bsr || O == Op::Braf ||
                       O == Op::Bsrf || O == Op::Jmp || O == Op::Jsr || O == Op::Rts) {
      const u32 pc4 = pc + 4;
      if constexpr (kBad) {
        // Slot illegal (5.5.3): the branch's own effects happen, then vector 6
        // with the target as the saved PC.  A conditional branch not taken
        // continues with the slot as an ordinary instruction.
        u32 target;
        if constexpr (O == Op::BfS || O == Op::BtS) {
          if ((O == Op::BtS) != t(cpu)) SH2_END(1);
          target = relative_pc(c, pc);
        }
        else if constexpr (O == Op::Bra || O == Op::Bsr) target = relative_pc(c, pc);
        else if constexpr (O == Op::Braf || O == Op::Bsrf) target = pc4 + r[m];
        else if constexpr (O == Op::Jmp || O == Op::Jsr) target = r[m];
        else target = cpu.regs_.pr;
        if constexpr (O == Op::Bsr || O == Op::Bsrf || O == Op::Jsr) cpu.regs_.pr = pc4;
        cpu.enter_exception(Cpu::kVecSlotIllegal, target, -1);
        SH2_JUMP(cpu.branch_to(cpu.regs_.pc), Cpu::kExcStates);
      } else {
        if constexpr (O == Op::BfS || O == Op::BtS) {
          if ((O == Op::BtS) == t(cpu)) arm_relative(cpu, c, pc); else arm_fallthrough(cpu, c, pc);
        }
        else if constexpr (O == Op::Bra || O == Op::Bsr) arm_relative(cpu, c, pc);
        else if constexpr (O == Op::Braf || O == Op::Bsrf) arm_absolute(cpu, pc4 + r[m]);
        else if constexpr (O == Op::Jmp || O == Op::Jsr) arm_absolute(cpu, r[m]);
        else arm_absolute(cpu, cpu.regs_.pr);
        if constexpr (O == Op::Bsr || O == Op::Bsrf || O == Op::Jsr) cpu.regs_.pr = pc4;
        cpu.branch_fetch_ = fetch;
        SH2_CHAIN_TO(slot, c);
      }
    }

    // ---- system control -------------------------------------------------------
    else if constexpr (O == Op::Clrt) { sr &= ~kT; SH2_END(1); }
    else if constexpr (O == Op::Sett) { sr |= kT; SH2_END(1); }
    else if constexpr (O == Op::Clrmac) { cpu.regs_.mach = cpu.regs_.macl = 0; SH2_END(1); }
    else if constexpr (O == Op::LdcSr) { cpu.set_sr(r[m]); cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::LdcGbr) { cpu.regs_.gbr = r[m]; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::LdcVbr) { cpu.regs_.vbr = r[m]; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::LdclSr) { cpu.set_sr(cpu.read32(r[m])); r[m] += 4; cpu.raise(Cpu::kPendDefer); SH2_END(3); }
    else if constexpr (O == Op::LdclGbr) { cpu.regs_.gbr = cpu.read32(r[m]); r[m] += 4; cpu.raise(Cpu::kPendDefer); SH2_END(3); }
    else if constexpr (O == Op::LdclVbr) { cpu.regs_.vbr = cpu.read32(r[m]); r[m] += 4; cpu.raise(Cpu::kPendDefer); SH2_END(3); }
    else if constexpr (O == Op::LdsMach) { cpu.regs_.mach = r[m]; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::LdsMacl) { cpu.regs_.macl = r[m]; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::LdsPr) { cpu.regs_.pr = r[m]; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::LdslMach) { cpu.regs_.mach = cpu.read32(r[m]); r[m] += 4; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::LdslMacl) { cpu.regs_.macl = cpu.read32(r[m]); r[m] += 4; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::LdslPr) { cpu.regs_.pr = cpu.read32(r[m]); r[m] += 4; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::Nop) { SH2_END(1); }
    else if constexpr (O == Op::Rte) {
      // The stack is popped before the slot runs; SR takes effect after it.
      const u32 new_pc = cpu.pop32();
      const u32 new_sr = cpu.pop32();
      if constexpr (kBad) {
        cpu.enter_exception(Cpu::kVecSlotIllegal, new_pc, -1);
        SH2_JUMP(cpu.branch_to(cpu.regs_.pc), Cpu::kExcStates);
      } else {
        cpu.branch_target_ = new_pc;
        cpu.branch_sr_ = new_sr;
        cpu.branch_finish_ = &finish_rte;
        cpu.branch_fetch_ = fetch;
        SH2_CHAIN_TO(slot, c);
      }
    }
    else if constexpr (O == Op::Sleep) {
      cpu.sleeping_ = true;
      cpu.standby_ = cpu.standby_request_;
      cpu.raise(Cpu::kPendSleep);
      SH2_END(3);
    }
    else if constexpr (O == Op::StcSr) { r[n] = sr; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::StcGbr) { r[n] = cpu.regs_.gbr; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::StcVbr) { r[n] = cpu.regs_.vbr; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::StclSr) { r[n] -= 4; cpu.write32(r[n], sr); cpu.raise(Cpu::kPendDefer); SH2_END(2); }
    else if constexpr (O == Op::StclGbr) { r[n] -= 4; cpu.write32(r[n], cpu.regs_.gbr); cpu.raise(Cpu::kPendDefer); SH2_END(2); }
    else if constexpr (O == Op::StclVbr) { r[n] -= 4; cpu.write32(r[n], cpu.regs_.vbr); cpu.raise(Cpu::kPendDefer); SH2_END(2); }
    else if constexpr (O == Op::StsMach) { r[n] = cpu.regs_.mach; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::StsMacl) { r[n] = cpu.regs_.macl; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::StsPr) { r[n] = cpu.regs_.pr; cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::StslMach) { r[n] -= 4; cpu.write32(r[n], cpu.regs_.mach); cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::StslMacl) { r[n] -= 4; cpu.write32(r[n], cpu.regs_.macl); cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::StslPr) { r[n] -= 4; cpu.write32(r[n], cpu.regs_.pr); cpu.raise(Cpu::kPendDefer); SH2_END(1); }
    else if constexpr (O == Op::Trapa) {
      cpu.enter_exception(u8(uimm), pc + 2, -1);
      SH2_JUMP(cpu.branch_to(cpu.regs_.pc), 8);
    }
    else {
      // General illegal instruction: PC saved is the start of the code.
      cpu.enter_exception(Cpu::kVecIllegal, pc, -1);
      SH2_JUMP(cpu.branch_to(cpu.regs_.pc), Cpu::kExcStates);
    }
  }

  // Instruction fetch from a space that raises an address error (peripheral
  // registers, reserved space).
  static const Cell* fetch_error(Cpu& cpu, const Cell* c) {
    const u8 fetch = 0;
    cpu.enter_exception(Cpu::kVecCpuAddrErr, cpu.pc_of(c), -1);
    SH2_JUMP(cpu.branch_to(cpu.regs_.pc), Cpu::kExcStates);
  }

  template <Op O>
  static Handler variant(Var v) {
    switch (v) {
      case Var::Plain: return &h<O, Var::Plain>;
      case Var::Line: return &h<O, Var::Line>;
      case Var::Slot:
        if constexpr (!writes_pc(O) && O != Op::Illegal) return &h<O, Var::Slot>;
        return nullptr;
      case Var::Bad:
        if constexpr (is_delayed_branch(O)) return &h<O, Var::Bad>;
        return nullptr;
      case Var::BadLine:
        if constexpr (is_delayed_branch(O)) return &h<O, Var::BadLine>;
        return nullptr;
    }
    return nullptr;
  }

  static Handler select(Op op, Var v) {
#define SH2_OP(name) case Op::name: return variant<Op::name>(v);
    switch (op) {
      SH2_OP(MovI) SH2_OP(MovwPc) SH2_OP(MovlPc) SH2_OP(MovR)
      SH2_OP(MovbS) SH2_OP(MovwS) SH2_OP(MovlS) SH2_OP(MovbL) SH2_OP(MovwL) SH2_OP(MovlL)
      SH2_OP(MovbM) SH2_OP(MovwM) SH2_OP(MovlM) SH2_OP(MovbP) SH2_OP(MovwP) SH2_OP(MovlP)
      SH2_OP(MovbS4) SH2_OP(MovwS4) SH2_OP(MovlS4) SH2_OP(MovbL4) SH2_OP(MovwL4) SH2_OP(MovlL4)
      SH2_OP(MovbS0) SH2_OP(MovwS0) SH2_OP(MovlS0) SH2_OP(MovbL0) SH2_OP(MovwL0) SH2_OP(MovlL0)
      SH2_OP(MovbSG) SH2_OP(MovwSG) SH2_OP(MovlSG) SH2_OP(MovbLG) SH2_OP(MovwLG) SH2_OP(MovlLG)
      SH2_OP(Mova) SH2_OP(Movt) SH2_OP(SwapB) SH2_OP(SwapW) SH2_OP(Xtrct)
      SH2_OP(Add) SH2_OP(AddI) SH2_OP(AddC) SH2_OP(AddV) SH2_OP(CmpEqI) SH2_OP(CmpEq) SH2_OP(CmpHs) SH2_OP(CmpGe)
      SH2_OP(CmpHi) SH2_OP(CmpGt) SH2_OP(CmpPl) SH2_OP(CmpPz) SH2_OP(CmpStr)
      SH2_OP(Div1) SH2_OP(Div0S) SH2_OP(Div0U) SH2_OP(DmulsL) SH2_OP(DmuluL) SH2_OP(Dt)
      SH2_OP(ExtsB) SH2_OP(ExtsW) SH2_OP(ExtuB) SH2_OP(ExtuW) SH2_OP(MacL) SH2_OP(MacW) SH2_OP(MulL) SH2_OP(MulsW) SH2_OP(MuluW)
      SH2_OP(Neg) SH2_OP(NegC) SH2_OP(Sub) SH2_OP(SubC) SH2_OP(SubV)
      SH2_OP(And) SH2_OP(AndI) SH2_OP(AndB) SH2_OP(Not) SH2_OP(Or) SH2_OP(OrI) SH2_OP(OrB) SH2_OP(Tas)
      SH2_OP(Tst) SH2_OP(TstI) SH2_OP(TstB) SH2_OP(Xor) SH2_OP(XorI) SH2_OP(XorB)
      SH2_OP(Rotl) SH2_OP(Rotr) SH2_OP(Rotcl) SH2_OP(Rotcr) SH2_OP(Shal) SH2_OP(Shar) SH2_OP(Shll) SH2_OP(Shlr)
      SH2_OP(Shll2) SH2_OP(Shlr2) SH2_OP(Shll8) SH2_OP(Shlr8) SH2_OP(Shll16) SH2_OP(Shlr16)
      SH2_OP(Bf) SH2_OP(BfS) SH2_OP(Bt) SH2_OP(BtS) SH2_OP(Bra) SH2_OP(Braf) SH2_OP(Bsr) SH2_OP(Bsrf) SH2_OP(Jmp) SH2_OP(Jsr) SH2_OP(Rts)
      SH2_OP(Clrt) SH2_OP(Clrmac) SH2_OP(LdcSr) SH2_OP(LdcGbr) SH2_OP(LdcVbr) SH2_OP(LdclSr) SH2_OP(LdclGbr) SH2_OP(LdclVbr)
      SH2_OP(LdsMach) SH2_OP(LdsMacl) SH2_OP(LdsPr) SH2_OP(LdslMach) SH2_OP(LdslMacl) SH2_OP(LdslPr)
      SH2_OP(Nop) SH2_OP(Rte) SH2_OP(Sett) SH2_OP(Sleep) SH2_OP(StcSr) SH2_OP(StcGbr) SH2_OP(StcVbr)
      SH2_OP(StclSr) SH2_OP(StclGbr) SH2_OP(StclVbr) SH2_OP(StsMach) SH2_OP(StsMacl) SH2_OP(StsPr)
      SH2_OP(StslMach) SH2_OP(StslMacl) SH2_OP(StslPr) SH2_OP(Trapa)
      default: return variant<Op::Illegal>(v);
    }
#undef SH2_OP
  }
};

// Decode the instruction word at `pc` into `c`.
namespace {
// instruction, modulo cache drops) on stderr.
}

void Cpu::fill_cell(Cell* c, u32 pc) {
  const u8 at = bus_.attr(pc);
  *c = Cell{};
  c->flags = u8(at & Bus::kClassMask);
  if (at & Bus::kNoExec) {
    c->fn = &ExecImpl::fetch_error;
    return;
  }
  DecodedInsn d = decode(bus_.read16(pc));
  if (cfg_.sh1 && is_sh2_only(d.op)) d = DecodedInsn{};  // SH-1: the SH-2 additions are illegal codes
  c->imm = d.imm;
  c->n = d.n;
  c->m = d.m;
  c->cyc = d.cycles;
  bus_.mark_code(pc);
  // External code pays for each 4-byte fetch line: through the cache model
  // when CCR enables the cache for the area, otherwise as a constant.
  const bool external = bus_.external(at);
  const bool cached = external && bus_.cacheable(at) && cache_covers(pc);
  const u8 static_fetch = external && !cached ? u8(std::min<u32>(bus_.fetch_static(at), 255)) : 0;
  const bool line_start = (pc & 3) == 0;

  if (is_delayed_branch(d.op)) {
    // Fuse the delay slot into the branch cell.
    const u32 slot_pc = pc + 2;
    const u8 slot_at = bus_.attr(slot_pc);
    const DecodedInsn sd = (slot_at & Bus::kNoExec) ? DecodedInsn{} : decode(bus_.read16(slot_pc));
    const bool bad = (slot_at & Bus::kNoExec) || sd.op == Op::Illegal || writes_pc(sd.op);
    if (bad) {
      c->fn = ExecImpl::select(d.op, cached && line_start ? Var::BadLine : Var::Bad);
      c->fetch = line_start ? static_fetch : 0;
    } else {
      // Exactly one of the two words starts a fetch line.
      c->fn = ExecImpl::select(d.op, cached ? Var::Line : Var::Plain);
      c->real = ExecImpl::select(sd.op, Var::Slot);
      c->slot_imm = sd.imm;
      c->slot_n = sd.n;
      c->slot_m = sd.m;
      c->slot_cyc = sd.cycles;
      c->fetch = static_fetch;
      if ((pc & (kPageSize - 1)) >= kPageSize - 4) c->flags |= kFlagPageEdge;
      bus_.mark_code(slot_pc);
    }
  } else {
    c->fn = ExecImpl::select(d.op, cached && line_start ? Var::Line : Var::Plain);
    c->fetch = line_start ? static_fetch : 0;
  }

  // PC-relative targets inside the page are resolved to a cell delta.
  if (d.op == Op::Bra || d.op == Op::Bsr || d.op == Op::Bf || d.op == Op::Bt || d.op == Op::BfS || d.op == Op::BtS) {
    const u32 target = pc + 4 + u32(d.imm);
    if ((target >> kPageShift) == (pc >> kPageShift)) {
      c->flags |= kFlagDirect;
      c->imm = s32(target - pc) / 2;
      if ((target & 2) && bus_.external(bus_.attr(target))) c->flags |= kFlagTargetLine;
    }
  }
}

namespace detail {

const Cell* fill(Cpu& cpu, const Cell* c) {
  cpu.fill_cell(const_cast<Cell*>(c), cpu.pc_of(c));
#if SH2_HAVE_MUSTTAIL
  [[clang::musttail]] return c->fn(cpu, c);
#else
  return c->fn(cpu, c);
#endif
}

// Guard cell after the last instruction of a page: continue in the next page.
const Cell* page_end(Cpu& cpu, const Cell*) {
  const Cell* nx = cpu.cells_for(cpu.page_pc_ + kPageSize);
  SH2_CHAIN(nx);
}

}  // namespace detail
}  // namespace sh2

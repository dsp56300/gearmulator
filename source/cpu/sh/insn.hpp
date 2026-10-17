// The instruction effects the SH cores share.
//
// Data transfer, arithmetic, logic, shifts and the control-register moves do
// the same thing on every member of the family; only how a core finishes an
// instruction (charging states, deferring interrupts, chaining to the next
// cell) and how it raises exceptions differ.  So a core's handler reads the
// operands from its cell, calls exec<O>() for the effect and finishes the
// instruction its own way.  Branches, RTE, SLEEP, TRAPA and anything that
// enters an exception stay with the core, as do the SH-3's SSR / SPC / bank
// register moves.
//
// Cpu provides regs_ (r, sr, gbr, pr, mach, macl), read8/16/32, write8/16/32
// and set_sr(), says how MAC.L saturates (kMacLSaturation) and declares
// `friend struct sh::Insn<Cpu>`.
#pragma once
#include <algorithm>

#include "cpu/sh/op.hpp"

namespace sh {

template <class Cpu>
struct Insn {
  static u32 sx8(u32 v) { return u32(s32(s8(v))); }
  static u32 sx16(u32 v) { return u32(s32(s16(v))); }
  static void set_t(Cpu& cpu, bool t) { cpu.regs_.sr = (cpu.regs_.sr & ~kSrT) | (t ? kSrT : 0); }
  static bool t(const Cpu& cpu) { return cpu.regs_.sr & kSrT; }

  // MAC.W / MAC.L accumulation with the S-bit saturation rules.
  static void mac_add(Cpu& cpu, s64 product, bool word) {
    auto& r = cpu.regs_;
    if (!(r.sr & kSrS)) {
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
    if constexpr (Cpu::kMacLSaturation == MacLSaturation::ManualPseudoCode) {
      // 48-bit saturation as the SH-2 manual's MAC.L pseudo-code does it: the
      // low 16 bits of MACH join the accumulation, the 32-bit signed result
      // decides the overflow, and the saturated MACH is H'00007FFF / H'00008000.
      const u64 sum = u64(r.macl) + u64(u32(product));
      u32 hi = u32(u64(product) >> 32) + u32(sum >> 32) + (r.mach & 0xFFFF);
      u32 lo = u32(sum);
      if (s32(hi) < 0 && hi < 0xFFFF8000u) { hi = 0x00008000u; lo = 0; }
      else if (s32(hi) > 0 && hi > 0x00007FFFu) { hi = 0x00007FFFu; lo = 0xFFFFFFFFu; }
      r.mach = hi;
      r.macl = lo;
    } else {
      // The 64-bit accumulator clamped to 48 bits, two's complement: MACH
      // keeps the sign extension (H'FFFF8000 / H'00007FFF).
      const s64 acc = s64((u64(r.mach) << 32) | r.macl);
      s64 sum;
      constexpr s64 kMax = (s64(1) << 47) - 1, kMin = -(s64(1) << 47);
      if (__builtin_add_overflow(acc, product, &sum)) sum = acc < 0 ? kMin : kMax;
      else sum = std::min(std::max(sum, kMin), kMax);
      r.mach = u32(u64(sum) >> 32);
      r.macl = u32(sum);
    }
  }

  // DIV1 single-step division, literally the manual's algorithm.
  static void div1(Cpu& cpu, unsigned n, unsigned m) {
    u32& sr = cpu.regs_.sr;
    u32& rn = cpu.regs_.r[n];
    const bool old_q = (sr & kSrQ) != 0, mbit = (sr & kSrM) != 0;
    bool q = (rn & 0x80000000u) != 0;
    rn = (rn << 1) | (sr & kSrT);
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
    sr = (sr & ~(kSrQ | kSrT)) | (q ? kSrQ : 0) | ((q == mbit) ? kSrT : 0);
  }

  // The instructions exec<O>() implements.  Everything else is the core's:
  // the branches, RTE, SLEEP, TRAPA, the illegal code and the SH-3's SSR /
  // SPC / bank moves.
  static constexpr bool handles(Op op) {
    switch (op) {
      case Op::Illegal:
      case Op::Bf: case Op::BfS: case Op::Bt: case Op::BtS: case Op::Bra: case Op::Braf: case Op::Bsr:
      case Op::Bsrf: case Op::Jmp: case Op::Jsr: case Op::Rts: case Op::Rte: case Op::Sleep: case Op::Trapa:
      case Op::LdcSsr: case Op::LdcSpc: case Op::LdcRbank: case Op::LdclSsr: case Op::LdclSpc: case Op::LdclRbank:
      case Op::StcSsr: case Op::StcSpc: case Op::StcRbank: case Op::StclSsr: case Op::StclSpc: case Op::StclRbank:
      case Op::kCount:
        return false;
      default:
        return true;
    }
  }

  // The effect of O with operands n, m and the (pre-scaled) immediate; `pc`
  // is the instruction's own address.
  template <Op O>
  static void exec(Cpu& cpu, u32 pc, unsigned n, unsigned m, u32 uimm) {
    static_assert(handles(O));
    u32* const r = cpu.regs_.r;
    u32& sr = cpu.regs_.sr;

    // ---- data transfer --------------------------------------------------------
    if constexpr (O == Op::MovI) r[n] = uimm;
    else if constexpr (O == Op::MovwPc) r[n] = sx16(cpu.read16(pc + 4 + uimm));
    else if constexpr (O == Op::MovlPc) r[n] = cpu.read32(((pc + 4) & ~3u) + uimm);
    else if constexpr (O == Op::MovR) r[n] = r[m];
    else if constexpr (O == Op::MovbS) cpu.write8(r[n], r[m]);
    else if constexpr (O == Op::MovwS) cpu.write16(r[n], r[m]);
    else if constexpr (O == Op::MovlS) cpu.write32(r[n], r[m]);
    else if constexpr (O == Op::MovbL) r[n] = sx8(cpu.read8(r[m]));
    else if constexpr (O == Op::MovwL) r[n] = sx16(cpu.read16(r[m]));
    else if constexpr (O == Op::MovlL) r[n] = cpu.read32(r[m]);
    else if constexpr (O == Op::MovbM) { const u32 v = r[m]; r[n] -= 1; cpu.write8(r[n], v); }
    else if constexpr (O == Op::MovwM) { const u32 v = r[m]; r[n] -= 2; cpu.write16(r[n], v); }
    else if constexpr (O == Op::MovlM) { const u32 v = r[m]; r[n] -= 4; cpu.write32(r[n], v); }
    else if constexpr (O == Op::MovbP) { const u32 v = sx8(cpu.read8(r[m])); if (n != m) r[m] += 1; r[n] = v; }
    else if constexpr (O == Op::MovwP) { const u32 v = sx16(cpu.read16(r[m])); if (n != m) r[m] += 2; r[n] = v; }
    else if constexpr (O == Op::MovlP) { const u32 v = cpu.read32(r[m]); if (n != m) r[m] += 4; r[n] = v; }
    else if constexpr (O == Op::MovbS4) cpu.write8(r[n] + uimm, r[0]);
    else if constexpr (O == Op::MovwS4) cpu.write16(r[n] + uimm, r[0]);
    else if constexpr (O == Op::MovlS4) cpu.write32(r[n] + uimm, r[m]);
    else if constexpr (O == Op::MovbL4) r[0] = sx8(cpu.read8(r[m] + uimm));
    else if constexpr (O == Op::MovwL4) r[0] = sx16(cpu.read16(r[m] + uimm));
    else if constexpr (O == Op::MovlL4) r[n] = cpu.read32(r[m] + uimm);
    else if constexpr (O == Op::MovbS0) cpu.write8(r[0] + r[n], r[m]);
    else if constexpr (O == Op::MovwS0) cpu.write16(r[0] + r[n], r[m]);
    else if constexpr (O == Op::MovlS0) cpu.write32(r[0] + r[n], r[m]);
    else if constexpr (O == Op::MovbL0) r[n] = sx8(cpu.read8(r[0] + r[m]));
    else if constexpr (O == Op::MovwL0) r[n] = sx16(cpu.read16(r[0] + r[m]));
    else if constexpr (O == Op::MovlL0) r[n] = cpu.read32(r[0] + r[m]);
    else if constexpr (O == Op::MovbSG) cpu.write8(cpu.regs_.gbr + uimm, r[0]);
    else if constexpr (O == Op::MovwSG) cpu.write16(cpu.regs_.gbr + uimm, r[0]);
    else if constexpr (O == Op::MovlSG) cpu.write32(cpu.regs_.gbr + uimm, r[0]);
    else if constexpr (O == Op::MovbLG) r[0] = sx8(cpu.read8(cpu.regs_.gbr + uimm));
    else if constexpr (O == Op::MovwLG) r[0] = sx16(cpu.read16(cpu.regs_.gbr + uimm));
    else if constexpr (O == Op::MovlLG) r[0] = cpu.read32(cpu.regs_.gbr + uimm);
    else if constexpr (O == Op::Mova) r[0] = ((pc + 4) & ~3u) + uimm;
    else if constexpr (O == Op::Movt) r[n] = sr & kSrT;
    else if constexpr (O == Op::SwapB) { const u32 v = r[m]; r[n] = (v & 0xFFFF0000u) | ((v & 0xFF) << 8) | ((v >> 8) & 0xFF); }
    else if constexpr (O == Op::SwapW) { const u32 v = r[m]; r[n] = (v << 16) | (v >> 16); }
    else if constexpr (O == Op::Xtrct) r[n] = (r[m] << 16) | (r[n] >> 16);
    // PREF is a cache hint and LDTLB loads an MMU nobody models: nothing to do.
    else if constexpr (O == Op::Pref || O == Op::Ldtlb) {}

    // ---- arithmetic -----------------------------------------------------------
    else if constexpr (O == Op::Add) r[n] += r[m];
    else if constexpr (O == Op::AddI) r[n] += uimm;
    else if constexpr (O == Op::AddC) {
      const u32 tmp1 = r[n] + r[m], tmp0 = r[n];
      r[n] = tmp1 + (sr & kSrT);
      set_t(cpu, tmp0 > tmp1 || tmp1 > r[n]);
    }
    else if constexpr (O == Op::AddV) {
      const s32 d = s32(r[n]), s = s32(r[m]);
      const s32 ans = s32(u32(d) + u32(s));
      set_t(cpu, ((d >= 0) == (s >= 0)) && ((ans >= 0) != (d >= 0)));
      r[n] = u32(ans);
    }
    else if constexpr (O == Op::CmpEqI) set_t(cpu, r[0] == uimm);
    else if constexpr (O == Op::CmpEq) set_t(cpu, r[n] == r[m]);
    else if constexpr (O == Op::CmpHs) set_t(cpu, r[n] >= r[m]);
    else if constexpr (O == Op::CmpGe) set_t(cpu, s32(r[n]) >= s32(r[m]));
    else if constexpr (O == Op::CmpHi) set_t(cpu, r[n] > r[m]);
    else if constexpr (O == Op::CmpGt) set_t(cpu, s32(r[n]) > s32(r[m]));
    else if constexpr (O == Op::CmpPl) set_t(cpu, s32(r[n]) > 0);
    else if constexpr (O == Op::CmpPz) set_t(cpu, s32(r[n]) >= 0);
    else if constexpr (O == Op::CmpStr) {
      const u32 x = r[n] ^ r[m];
      set_t(cpu, !(x & 0xFF000000u) || !(x & 0x00FF0000u) || !(x & 0x0000FF00u) || !(x & 0x000000FFu));
    }
    else if constexpr (O == Op::Div1) div1(cpu, n, m);
    else if constexpr (O == Op::Div0S) {
      const u32 q = r[n] >> 31, mm = r[m] >> 31;
      sr = (sr & ~(kSrQ | kSrM | kSrT)) | (q ? kSrQ : 0) | (mm ? kSrM : 0) | ((q ^ mm) ? kSrT : 0);
    }
    else if constexpr (O == Op::Div0U) sr &= ~(kSrQ | kSrM | kSrT);
    else if constexpr (O == Op::DmulsL) {
      const s64 p = s64(s32(r[n])) * s64(s32(r[m]));
      cpu.regs_.mach = u32(u64(p) >> 32);
      cpu.regs_.macl = u32(p);
    }
    else if constexpr (O == Op::DmuluL) {
      const u64 p = u64(r[n]) * u64(r[m]);
      cpu.regs_.mach = u32(p >> 32);
      cpu.regs_.macl = u32(p);
    }
    else if constexpr (O == Op::Dt) { r[n] -= 1; set_t(cpu, r[n] == 0); }
    else if constexpr (O == Op::ExtsB) r[n] = sx8(r[m]);
    else if constexpr (O == Op::ExtsW) r[n] = sx16(r[m]);
    else if constexpr (O == Op::ExtuB) r[n] = r[m] & 0xFF;
    else if constexpr (O == Op::ExtuW) r[n] = r[m] & 0xFFFF;
    else if constexpr (O == Op::MacL) {
      const s32 a = s32(cpu.read32(r[n]));
      r[n] += 4;
      const s32 b = s32(cpu.read32(r[m]));
      r[m] += 4;
      mac_add(cpu, s64(a) * s64(b), false);
    }
    else if constexpr (O == Op::MacW) {
      const s32 a = s32(s16(cpu.read16(r[n])));
      r[n] += 2;
      const s32 b = s32(s16(cpu.read16(r[m])));
      r[m] += 2;
      mac_add(cpu, s64(a) * s64(b), true);
    }
    else if constexpr (O == Op::MulL) cpu.regs_.macl = r[n] * r[m];
    else if constexpr (O == Op::MulsW) cpu.regs_.macl = u32(s32(s16(r[n])) * s32(s16(r[m])));
    else if constexpr (O == Op::MuluW) cpu.regs_.macl = u32(u16(r[n])) * u32(u16(r[m]));
    else if constexpr (O == Op::Neg) r[n] = 0u - r[m];
    else if constexpr (O == Op::NegC) {
      const u32 tmp = 0u - r[m];
      r[n] = tmp - (sr & kSrT);
      set_t(cpu, 0 < tmp || tmp < r[n]);
    }
    else if constexpr (O == Op::Sub) r[n] -= r[m];
    else if constexpr (O == Op::SubC) {
      const u32 tmp1 = r[n] - r[m], tmp0 = r[n];
      r[n] = tmp1 - (sr & kSrT);
      set_t(cpu, tmp0 < tmp1 || tmp1 < r[n]);
    }
    else if constexpr (O == Op::SubV) {
      const s32 d = s32(r[n]), s = s32(r[m]);
      const s32 ans = s32(u32(d) - u32(s));
      set_t(cpu, ((d >= 0) != (s >= 0)) && ((ans >= 0) != (d >= 0)));
      r[n] = u32(ans);
    }

    // ---- logic ----------------------------------------------------------------
    else if constexpr (O == Op::And) r[n] &= r[m];
    else if constexpr (O == Op::AndI) r[0] &= uimm;
    else if constexpr (O == Op::AndB) { const u32 a = cpu.regs_.gbr + r[0]; cpu.write8(a, cpu.read8(a) & uimm); }
    else if constexpr (O == Op::Not) r[n] = ~r[m];
    else if constexpr (O == Op::Or) r[n] |= r[m];
    else if constexpr (O == Op::OrI) r[0] |= uimm;
    else if constexpr (O == Op::OrB) { const u32 a = cpu.regs_.gbr + r[0]; cpu.write8(a, cpu.read8(a) | uimm); }
    else if constexpr (O == Op::Tas) {
      const u32 v = cpu.read8(r[n]);
      set_t(cpu, v == 0);
      cpu.write8(r[n], v | 0x80);
    }
    else if constexpr (O == Op::Tst) set_t(cpu, (r[n] & r[m]) == 0);
    else if constexpr (O == Op::TstI) set_t(cpu, (r[0] & uimm) == 0);
    else if constexpr (O == Op::TstB) set_t(cpu, (cpu.read8(cpu.regs_.gbr + r[0]) & uimm) == 0);
    else if constexpr (O == Op::Xor) r[n] ^= r[m];
    else if constexpr (O == Op::XorI) r[0] ^= uimm;
    else if constexpr (O == Op::XorB) { const u32 a = cpu.regs_.gbr + r[0]; cpu.write8(a, cpu.read8(a) ^ uimm); }

    // ---- shifts ---------------------------------------------------------------
    else if constexpr (O == Op::Rotl) { const u32 hi = r[n] >> 31; r[n] = (r[n] << 1) | hi; set_t(cpu, hi); }
    else if constexpr (O == Op::Rotr) { const u32 lo = r[n] & 1; r[n] = (r[n] >> 1) | (lo << 31); set_t(cpu, lo); }
    else if constexpr (O == Op::Rotcl) { const u32 hi = r[n] >> 31; r[n] = (r[n] << 1) | (sr & kSrT); set_t(cpu, hi); }
    else if constexpr (O == Op::Rotcr) { const u32 lo = r[n] & 1; r[n] = (r[n] >> 1) | ((sr & kSrT) << 31); set_t(cpu, lo); }
    else if constexpr (O == Op::Shad) {
      const u32 s = r[m];
      if (!(s & 0x80000000u)) r[n] <<= (s & 31);
      else if ((s & 31) == 0) r[n] = (r[n] & 0x80000000u) ? 0xFFFFFFFFu : 0;
      else r[n] = u32(s32(r[n]) >> ((~s & 31) + 1));
    }
    else if constexpr (O == Op::Shld) {
      const u32 s = r[m];
      if (!(s & 0x80000000u)) r[n] <<= (s & 31);
      else if ((s & 31) == 0) r[n] = 0;
      else r[n] >>= ((~s & 31) + 1);
    }
    else if constexpr (O == Op::Shal || O == Op::Shll) { set_t(cpu, r[n] >> 31); r[n] <<= 1; }
    else if constexpr (O == Op::Shar) { set_t(cpu, r[n] & 1); r[n] = u32(s32(r[n]) >> 1); }
    else if constexpr (O == Op::Shlr) { set_t(cpu, r[n] & 1); r[n] >>= 1; }
    else if constexpr (O == Op::Shll2) r[n] <<= 2;
    else if constexpr (O == Op::Shlr2) r[n] >>= 2;
    else if constexpr (O == Op::Shll8) r[n] <<= 8;
    else if constexpr (O == Op::Shlr8) r[n] >>= 8;
    else if constexpr (O == Op::Shll16) r[n] <<= 16;
    else if constexpr (O == Op::Shlr16) r[n] >>= 16;

    // ---- system control -------------------------------------------------------
    else if constexpr (O == Op::Clrt) sr &= ~kSrT;
    else if constexpr (O == Op::Sett) sr |= kSrT;
    else if constexpr (O == Op::Clrs) sr &= ~kSrS;
    else if constexpr (O == Op::Sets) sr |= kSrS;
    else if constexpr (O == Op::Clrmac) cpu.regs_.mach = cpu.regs_.macl = 0;
    else if constexpr (O == Op::Nop) {}
    else if constexpr (O == Op::LdcSr) cpu.set_sr(r[m]);
    else if constexpr (O == Op::LdcGbr) cpu.regs_.gbr = r[m];
    else if constexpr (O == Op::LdcVbr) cpu.regs_.vbr = r[m];
    else if constexpr (O == Op::LdclSr) { cpu.set_sr(cpu.read32(r[m])); r[m] += 4; }
    else if constexpr (O == Op::LdclGbr) { cpu.regs_.gbr = cpu.read32(r[m]); r[m] += 4; }
    else if constexpr (O == Op::LdclVbr) { cpu.regs_.vbr = cpu.read32(r[m]); r[m] += 4; }
    else if constexpr (O == Op::LdsMach) cpu.regs_.mach = r[m];
    else if constexpr (O == Op::LdsMacl) cpu.regs_.macl = r[m];
    else if constexpr (O == Op::LdsPr) cpu.regs_.pr = r[m];
    else if constexpr (O == Op::LdslMach) { cpu.regs_.mach = cpu.read32(r[m]); r[m] += 4; }
    else if constexpr (O == Op::LdslMacl) { cpu.regs_.macl = cpu.read32(r[m]); r[m] += 4; }
    else if constexpr (O == Op::LdslPr) { cpu.regs_.pr = cpu.read32(r[m]); r[m] += 4; }
    else if constexpr (O == Op::StcSr) r[n] = sr;
    else if constexpr (O == Op::StcGbr) r[n] = cpu.regs_.gbr;
    else if constexpr (O == Op::StcVbr) r[n] = cpu.regs_.vbr;
    else if constexpr (O == Op::StclSr) { r[n] -= 4; cpu.write32(r[n], sr); }
    else if constexpr (O == Op::StclGbr) { r[n] -= 4; cpu.write32(r[n], cpu.regs_.gbr); }
    else if constexpr (O == Op::StclVbr) { r[n] -= 4; cpu.write32(r[n], cpu.regs_.vbr); }
    else if constexpr (O == Op::StsMach) r[n] = cpu.regs_.mach;
    else if constexpr (O == Op::StsMacl) r[n] = cpu.regs_.macl;
    else if constexpr (O == Op::StsPr) r[n] = cpu.regs_.pr;
    else if constexpr (O == Op::StslMach) { r[n] -= 4; cpu.write32(r[n], cpu.regs_.mach); }
    else if constexpr (O == Op::StslMacl) { r[n] -= 4; cpu.write32(r[n], cpu.regs_.macl); }
    else if constexpr (O == Op::StslPr) { r[n] -= 4; cpu.write32(r[n], cpu.regs_.pr); }
    else static_assert(O != O, "every handled Op needs an effect");
  }
};

}  // namespace sh

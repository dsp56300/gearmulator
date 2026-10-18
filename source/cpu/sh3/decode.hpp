// SH-3 instruction decoder (SH-3/SH-3E/SH3-DSP programming manual): the
// family decoder with this core's view of it.  The set is the SH-2's plus
// the SH-3 additions (SHAD / SHLD, PREF, LDTLB, CLRS / SETS and the SSR /
// SPC / Rn_BANK control register transfers); the SH-4-only codes decode as
// illegal.  The cycle counts are the manual's issue rates.
#pragma once
#include "cpu/sh/decode.hpp"
#include "cpu/sh3/types.hpp"

namespace sh3 {

using sh::DecodedInsn;
using sh::Op;
using sh::is_delayed_branch;
using sh::writes_pc;

struct Isa {
  static constexpr bool legal(Op) { return true; }
  static constexpr u8 cycles(Op op) {
    switch (op) {
      case Op::Bsrf: case Op::Braf: case Op::Bra: case Op::Bsr: case Op::Bt: case Op::Bf: case Op::BtS: case Op::BfS:
      case Op::Jmp: case Op::Jsr: case Op::Rts:
      case Op::MulL: case Op::DmulsL: case Op::DmuluL: case Op::MacL: case Op::MacW:
      case Op::StclSr: case Op::StclGbr: case Op::StclVbr: case Op::StclSsr: case Op::StclSpc: case Op::StclRbank:
        return 2;
      case Op::LdclGbr: case Op::LdclVbr: case Op::LdclSsr: case Op::LdclSpc:
      case Op::LdcGbr: case Op::LdcVbr: case Op::LdcSsr: case Op::LdcSpc:
      case Op::TstB: case Op::AndB: case Op::XorB: case Op::OrB:
        return 3;
      case Op::Sleep: case Op::Rte: case Op::Tas: case Op::LdcSr: case Op::LdclSr:
        return 4;
      case Op::Trapa:
        return 8;
      default:
        return 1;
    }
  }
};

inline DecodedInsn decode(u16 code) { return sh::decode<Isa>(code); }

}  // namespace sh3

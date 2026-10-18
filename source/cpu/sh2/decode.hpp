// SH-2 instruction decoder (SH7014/16/17 hardware manual section 2.4): the
// family decoder with this core's view of it.  The SH-3 additions are illegal
// codes here; the cycle counts are the base execution cycles of tables
// 2.12-2.17.
#pragma once
#include "cpu/sh/decode.hpp"
#include "cpu/sh2/types.hpp"

namespace sh2 {

using sh::DecodedInsn;
using sh::Op;
using sh::disables_interrupt;
using sh::is_delayed_branch;
using sh::is_sh2_only;
using sh::writes_pc;

struct Isa {
  static constexpr bool legal(Op op) { return !sh::is_sh3_only(op); }
  static constexpr u8 cycles(Op op) {
    switch (op) {
      case Op::Bsrf: case Op::Braf: case Op::Bra: case Op::Bsr: case Op::BtS: case Op::BfS:
      case Op::Jmp: case Op::Jsr: case Op::Rts:
      case Op::MulL: case Op::DmulsL: case Op::DmuluL:
      case Op::StclSr: case Op::StclGbr: case Op::StclVbr:
        return 2;
      case Op::Bt: case Op::Bf: case Op::Sleep: case Op::MacL: case Op::MacW:
      case Op::LdclSr: case Op::LdclGbr: case Op::LdclVbr:
      case Op::TstB: case Op::AndB: case Op::XorB: case Op::OrB:
        return 3;
      case Op::Rte: case Op::Tas:
        return 4;
      case Op::Trapa:
        return 8;
      default:
        return 1;
    }
  }
};

inline DecodedInsn decode(u16 code) { return sh::decode<Isa>(code); }

}  // namespace sh2

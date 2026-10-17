// SH-3 instruction handlers and cell fill.
//
// One template `h<Op, Var>` implements every instruction; the variant says how
// the cell is executed.  Plain executes and chains to the next cell.  Slot
// executes the instruction as the delay slot of the branch that owns the cell
// (operands from the slot fields) and chains to the branch's completion.  Bad
// is a delayed branch whose slot holds a slot-illegal instruction.  The
// effects of the ordinary instructions are the family's (sh::Insn); the
// branches, the register banks and the exceptions follow the SH-3
// programming manual.
#include <algorithm>
#include <utility>

#include "common/host.hpp"
#include "cpu/sh3/cpu.hpp"

namespace sh3 {

#if EMU_HAS_MUSTTAIL
#define SH3_CHAIN(nx) [[clang::musttail]] return (nx)->fn(cpu, (nx))
#define SH3_CHAIN_TO(handler, cell) [[clang::musttail]] return (handler)(cpu, (cell))
#else
#define SH3_CHAIN(nx) return (nx)
#define SH3_CHAIN_TO(handler, cell) return (handler)(cpu, (cell))
#endif

// Finish an instruction: charge its states, then either continue with the
// branch completion (delay slot) or with the following cell.
#define SH3_END(states)                                        \
  do {                                                         \
    ++cpu.insn_count_;                                         \
    cpu.budget_ -= s32(states);                                \
    if constexpr (kSlot) {                                     \
      SH3_CHAIN_TO(cpu.branch_finish_, c);                     \
    } else {                                                   \
      const Cell* nx_ = c + 1;                                 \
      if (EMU_UNLIKELY(cpu.budget_ <= 0)) return nx_;          \
      SH3_CHAIN(nx_);                                          \
    }                                                          \
  } while (0)
// Finish a taken branch / exception: continue at `target`.
#define SH3_JUMP(target, states)                               \
  do {                                                         \
    cpu.budget_ -= s32(states);                                \
    ++cpu.insn_count_;                                         \
    const Cell* nx_ = (target);                                \
    if (EMU_UNLIKELY(cpu.budget_ <= 0)) return nx_;            \
    SH3_CHAIN(nx_);                                            \
  } while (0)

enum class Var : u8 { Plain, Slot, Bad };

struct ExecImpl {
  using Insn = sh::Insn<Cpu>;

  // ---- delayed branches ---------------------------------------------------------
  template <unsigned kStates>
  static const Cell* finish_direct(Cpu& cpu, const Cell*) { SH3_JUMP(cpu.branch_cell_, kStates); }
  template <unsigned kStates>
  static const Cell* finish_indirect(Cpu& cpu, const Cell*) { SH3_JUMP(cpu.branch_to(cpu.branch_target_), kStates); }
  static const Cell* relative_target(Cpu& cpu, const Cell* c, u32 pc) {
    if (c->flags & kFlagDirect) return c + c->imm;
    return cpu.branch_to(pc + 4 + u32(c->imm));
  }
  static void arm_relative(Cpu& cpu, const Cell* c, u32 pc) {
    if (c->flags & kFlagDirect) {
      cpu.branch_cell_ = c + c->imm;
      cpu.branch_finish_ = &finish_direct<2>;
    } else {
      cpu.branch_target_ = pc + 4 + u32(c->imm);
      cpu.branch_finish_ = &finish_indirect<2>;
    }
  }
  static void arm_fallthrough(Cpu& cpu, const Cell* c, u32 pc) {
    if (EMU_UNLIKELY(c->flags & kFlagPageEdge)) {
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

  template <Op O, Var V>
  static const Cell* h(Cpu& cpu, const Cell* c) {
    constexpr bool kSlot = V == Var::Slot;
    constexpr bool kBad = V == Var::Bad;
    static_assert(!kSlot || !writes_pc(O));
    static_assert(!kBad || is_delayed_branch(O));
    // Device hooks ask for the executing instruction's address; only an
    // instruction that touches memory can reach one.
    if constexpr (sh::accesses_memory(O) || O == Op::Illegal) cpu.cur_ip_ = c;
    const u32 pc = cpu.pc_of(c) + (kSlot ? 2 : 0);
    u32* const r = cpu.regs_.r;
    const unsigned n = kSlot ? c->slot_n : c->n, m = kSlot ? c->slot_m : c->m;
    const s32 imm = kSlot ? c->slot_imm : c->imm;
    const u32 uimm = u32(imm);
    [[maybe_unused]] const Handler slot = c->real;

    // ---- the family's instructions ---------------------------------------------
    if constexpr (Insn::handles(O)) {
      Insn::template exec<O>(cpu, pc, n, m, uimm);
      SH3_END(Isa::cycles(O));
    }

    // ---- the SH-3's control registers ------------------------------------------
    else if constexpr (O == Op::LdcSsr) { cpu.regs_.ssr = r[m] & kSrMask; SH3_END(Isa::cycles(O)); }
    else if constexpr (O == Op::LdcSpc) { cpu.regs_.spc = r[m]; SH3_END(Isa::cycles(O)); }
    else if constexpr (O == Op::LdcRbank) { cpu.bank_[m & 7] = r[n]; SH3_END(Isa::cycles(O)); }
    else if constexpr (O == Op::LdclSsr) { cpu.regs_.ssr = cpu.read32(r[m]) & kSrMask; r[m] += 4; SH3_END(Isa::cycles(O)); }
    else if constexpr (O == Op::LdclSpc) { cpu.regs_.spc = cpu.read32(r[m]); r[m] += 4; SH3_END(Isa::cycles(O)); }
    else if constexpr (O == Op::LdclRbank) { cpu.bank_[m & 7] = cpu.read32(r[n]); r[n] += 4; SH3_END(Isa::cycles(O)); }
    else if constexpr (O == Op::StcSsr) { r[n] = cpu.regs_.ssr; SH3_END(Isa::cycles(O)); }
    else if constexpr (O == Op::StcSpc) { r[n] = cpu.regs_.spc; SH3_END(Isa::cycles(O)); }
    else if constexpr (O == Op::StcRbank) { r[n] = cpu.bank_[m & 7]; SH3_END(Isa::cycles(O)); }
    else if constexpr (O == Op::StclSsr) { r[n] -= 4; cpu.write32(r[n], cpu.regs_.ssr); SH3_END(Isa::cycles(O)); }
    else if constexpr (O == Op::StclSpc) { r[n] -= 4; cpu.write32(r[n], cpu.regs_.spc); SH3_END(Isa::cycles(O)); }
    else if constexpr (O == Op::StclRbank) { r[n] -= 4; cpu.write32(r[n], cpu.bank_[m & 7]); SH3_END(Isa::cycles(O)); }

    // ---- branches -------------------------------------------------------------
    else if constexpr (O == Op::Bf || O == Op::Bt) {
      const bool taken = (O == Op::Bt) == Insn::t(cpu);
      if (!taken) SH3_END(1);
      SH3_JUMP(relative_target(cpu, c, pc), Isa::cycles(O));
    }
    else if constexpr (O == Op::BfS || O == Op::BtS || O == Op::Bra || O == Op::Bsr || O == Op::Braf ||
                       O == Op::Bsrf || O == Op::Jmp || O == Op::Jsr || O == Op::Rts) {
      const u32 pc4 = pc + 4;
      if constexpr (kBad) {
        // Slot illegal: the branch's own effects happen, then the general
        // exception with the branch as the saved PC.  A conditional branch
        // not taken continues with the slot as an ordinary instruction.
        if constexpr (O == Op::BfS || O == Op::BtS) {
          if ((O == Op::BtS) != Insn::t(cpu)) SH3_END(1);
        }
        if constexpr (O == Op::Bsr || O == Op::Bsrf || O == Op::Jsr) cpu.regs_.pr = pc4;
        cpu.enter_exception(kExcSlotIllegal, pc, kVecGeneral, false);
        SH3_JUMP(cpu.branch_to(cpu.regs_.pc), Cpu::kExcStates);
      } else {
        if constexpr (O == Op::BfS || O == Op::BtS) {
          if ((O == Op::BtS) == Insn::t(cpu)) arm_relative(cpu, c, pc); else arm_fallthrough(cpu, c, pc);
        }
        else if constexpr (O == Op::Bra || O == Op::Bsr) arm_relative(cpu, c, pc);
        else if constexpr (O == Op::Braf || O == Op::Bsrf) arm_absolute(cpu, pc4 + r[m]);
        else if constexpr (O == Op::Jmp || O == Op::Jsr) arm_absolute(cpu, r[m]);
        else arm_absolute(cpu, cpu.regs_.pr);
        if constexpr (O == Op::Bsr || O == Op::Bsrf || O == Op::Jsr) cpu.regs_.pr = pc4;
        SH3_CHAIN_TO(slot, c);
      }
    }

    // ---- the exception model's own instructions --------------------------------
    else if constexpr (O == Op::Rte) {
      // SR is restored before the delay slot runs; the branch goes to SPC.
      const u32 new_pc = cpu.regs_.spc;
      if constexpr (kBad) {
        cpu.enter_exception(kExcSlotIllegal, pc, kVecGeneral, false);
        SH3_JUMP(cpu.branch_to(cpu.regs_.pc), Cpu::kExcStates);
      } else {
        cpu.set_sr(cpu.regs_.ssr);
        arm_absolute(cpu, new_pc);
        cpu.branch_finish_ = &finish_indirect<Isa::cycles(Op::Rte)>;
        SH3_CHAIN_TO(slot, c);
      }
    }
    else if constexpr (O == Op::Sleep) {
      cpu.sleeping_ = true;
      cpu.raise(Cpu::kPendSleep);
      SH3_END(Isa::cycles(O));
    }
    else if constexpr (O == Op::Trapa) {
      cpu.tra_ = (uimm & 0xFF) << 2;
      cpu.enter_exception(kExcTrapa, pc + 2, kVecGeneral, false);
      SH3_JUMP(cpu.branch_to(cpu.regs_.pc), Isa::cycles(O));
    }
    else {
      // General illegal instruction: PC saved is the start of the instruction.
      cpu.enter_exception(kSlot ? kExcSlotIllegal : kExcIllegal, pc - (kSlot ? 2 : 0), kVecGeneral, false);
      SH3_JUMP(cpu.branch_to(cpu.regs_.pc), Cpu::kExcStates);
    }
  }

  // Instruction fetch from a space that cannot be executed: the manual's
  // instruction address error.
  static const Cell* fetch_error(Cpu& cpu, const Cell* c) {
    cpu.enter_exception(kExcAddrErrRead, cpu.pc_of(c), kVecGeneral, false);
    SH3_JUMP(cpu.branch_to(cpu.regs_.pc), Cpu::kExcStates);
  }

  template <Op O>
  static Handler variant(Var v) {
    switch (v) {
      case Var::Plain: return &h<O, Var::Plain>;
      case Var::Slot:
        if constexpr (!writes_pc(O) && O != Op::Illegal) return &h<O, Var::Slot>;
        return nullptr;
      case Var::Bad:
        if constexpr (is_delayed_branch(O)) return &h<O, Var::Bad>;
        return nullptr;
    }
    return nullptr;
  }

  static Handler select(Op op, Var v) {
#define SH3_OP(name)                                                       \
  case Op::name:                                                           \
    if constexpr (Isa::legal(Op::name)) return variant<Op::name>(v);       \
    else return variant<Op::Illegal>(v);
    switch (op) {
      SH_FOR_EACH_OP(SH3_OP)
      default: return variant<Op::Illegal>(v);
    }
#undef SH3_OP
  }
};

void Cpu::fill_cell(Cell* c, u32 pc) {
  const u8 at = bus_.attr(pc);
  *c = Cell{};
  c->flags = u8(at & Bus::kClassMask);
  if (at & Bus::kNoExec) {
    c->fn = &ExecImpl::fetch_error;
    return;
  }
  const DecodedInsn d = decode(bus_.read16(pc));
  c->imm = d.imm;
  c->n = d.n;
  c->m = d.m;
  c->cyc = d.cycles;
  bus_.mark_code(pc);

  if (is_delayed_branch(d.op)) {
    const u32 slot_pc = pc + 2;
    const u8 slot_at = bus_.attr(slot_pc);
    const DecodedInsn sd = (slot_at & Bus::kNoExec) ? DecodedInsn{} : decode(bus_.read16(slot_pc));
    const bool bad = (slot_at & Bus::kNoExec) || sd.op == Op::Illegal || writes_pc(sd.op);
    if (bad) {
      c->fn = ExecImpl::select(d.op, Var::Bad);
    } else {
      c->fn = ExecImpl::select(d.op, Var::Plain);
      c->real = ExecImpl::select(sd.op, Var::Slot);
      c->slot_imm = sd.imm;
      c->slot_n = sd.n;
      c->slot_m = sd.m;
      c->slot_cyc = sd.cycles;
      if ((pc & (kPageSize - 1)) >= kPageSize - 4) c->flags |= kFlagPageEdge;
      bus_.mark_code(slot_pc);
    }
  } else {
    c->fn = ExecImpl::select(d.op, Var::Plain);
  }

  // PC-relative targets inside the page are resolved to a cell delta.
  if (d.op == Op::Bra || d.op == Op::Bsr || d.op == Op::Bf || d.op == Op::Bt || d.op == Op::BfS || d.op == Op::BtS) {
    const u32 target = pc + 4 + u32(d.imm);
    if ((target >> kPageShift) == (pc >> kPageShift)) {
      c->flags |= kFlagDirect;
      c->imm = s32(target - pc) / 2;
    }
  }
}

namespace detail {

const Cell* fill(Cpu& cpu, const Cell* c) {
  cpu.fill_cell(const_cast<Cell*>(c), cpu.pc_of(c));
  EMU_TAILCALL(c->fn(cpu, c));
}

// Guard cell after the last instruction of a page: continue in the next page.
const Cell* page_end(Cpu& cpu, const Cell*) {
  const Cell* nx = cpu.cells_for(cpu.page_pc_ + kPageSize);
  SH3_CHAIN(nx);
}

}  // namespace detail
}  // namespace sh3

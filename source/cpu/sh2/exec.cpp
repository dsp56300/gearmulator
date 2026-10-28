// SH-2 instruction handlers and cell fill.
//
// One template `h<Op, Var>` implements every instruction; the variant says how
// the cell is executed.  Plain executes and chains to the next cell.  Line
// first charges the external fetch of the 4-byte line through the instruction
// cache and BSC models. Slot executes the instruction as the delay slot of the branch
// that owns the cell (operands from the slot fields) and chains to the
// branch's completion.  Bad / BadLine are delayed branches whose slot holds a
// slot-illegal instruction.  The effects of the ordinary instructions are the
// family's (sh::Insn); the branches and the exceptions follow the SH-1/SH-2
// programming manual.
#include <algorithm>
#include <utility>

#include "common/host.hpp"
#include "cpu/sh2/cpu.hpp"

namespace sh2 {

// Whether handlers chain by tail call is a property of the host: common/host.hpp.
#if EMU_HAS_MUSTTAIL
#define SH2_CHAIN(nx) [[clang::musttail]] return (nx)->fn(cpu, (nx))
#define SH2_CHAIN_TO(handler, cell) [[clang::musttail]] return (handler)(cpu, (cell))
#else
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
      if (EMU_UNLIKELY(cpu.budget_ <= 0)) return nx_;          \
      SH2_CHAIN(nx_);                                          \
    }                                                          \
  } while (0)
// Finish a taken branch / exception: continue at `target`.
#define SH2_JUMP(target, states)                               \
  do {                                                         \
    cpu.budget_ -= s32(states) + fetch;                        \
    ++cpu.insn_count_;                                         \
    const Cell* nx_ = (target);                                \
    if (EMU_UNLIKELY(cpu.budget_ <= 0)) return nx_;            \
    SH2_CHAIN(nx_);                                            \
  } while (0)

enum class Var : u8 { Plain, Line, Slot, Bad, BadLine };

struct ExecImpl {
  using Insn = sh::Insn<Cpu>;

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
      if (EMU_UNLIKELY(c->flags & kFlagTargetLine)) cpu.fetch_line_at(pc + u32(c->imm) * 2);
      return c + c->imm;
    }
    return cpu.branch_to(pc + 4 + u32(c->imm));
  }
  static void arm_relative(Cpu& cpu, const Cell* c, u32 pc) {
    if (c->flags & kFlagDirect) {
      if (EMU_UNLIKELY(c->flags & kFlagTargetLine)) cpu.fetch_line_at(pc + u32(c->imm) * 2);
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
    constexpr bool kLine = V == Var::Line || V == Var::BadLine;
    constexpr bool kBad = V == Var::Bad || V == Var::BadLine;
    static_assert(!kSlot || !writes_pc(O));
    static_assert(!kBad || is_delayed_branch(O));
    // Everything the cell provides is read up front: the instruction may
    // drop the decoded cells (a store into code, a bus retiming).
    const u32 pc = cpu.pc_of(c) + (kSlot ? 2 : 0);
    u32* const r = cpu.regs_.r;
    const unsigned n = kSlot ? c->slot_n : c->n, m = kSlot ? c->slot_m : c->m;
    const s32 imm = kSlot ? c->slot_imm : c->imm;
    const u32 uimm = u32(imm);
    [[maybe_unused]] const u8 fetch = c->fetch;
    [[maybe_unused]] const Handler slot = c->real;
    if constexpr (kLine) {
      // A fused delayed branch owns exactly one line start: its own word or its slot's.
      cpu.fetch_line_at(is_delayed_branch(O) && !kBad ? (pc + 2) & ~3u : pc);
    }

    // ---- the family's instructions ---------------------------------------------
    if constexpr (Insn::handles(O)) {
      if constexpr (O == Op::MovwPc) r[n] = u32(s32(s16(cpu.read16(pc + 4 + uimm, true))));
      else if constexpr (O == Op::MovlPc) r[n] = cpu.read32(((pc + 4) & ~3u) + uimm, true);
      else Insn::template exec<O>(cpu, pc, n, m, uimm);
      // Table 5.9 note 2: no interrupt is accepted after a control / system
      // register move.
      if constexpr (disables_interrupt(O)) cpu.raise(Cpu::kPendDefer);
      SH2_END(Isa::cycles(O));
    }

    // ---- branches -------------------------------------------------------------
    else if constexpr (O == Op::Bf || O == Op::Bt) {
      const bool taken = (O == Op::Bt) == Insn::t(cpu);
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
          if ((O == Op::BtS) != Insn::t(cpu)) SH2_END(1);
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
          if ((O == Op::BtS) == Insn::t(cpu)) arm_relative(cpu, c, pc); else arm_fallthrough(cpu, c, pc);
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

    // ---- the exception model's own instructions --------------------------------
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
      SH2_END(Isa::cycles(O));
    }
    else if constexpr (O == Op::Trapa) {
      cpu.enter_exception(u8(uimm), pc + 2, -1);
      SH2_JUMP(cpu.branch_to(cpu.regs_.pc), Isa::cycles(O));
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

  // The handler for an instruction: the codes this core does not execute get
  // no handlers of their own, the decoder never produces them.
  static Handler select(Op op, Var v) {
#define SH2_OP(name)                                                       \
  case Op::name:                                                           \
    if constexpr (Isa::legal(Op::name)) return variant<Op::name>(v);       \
    else return variant<Op::Illegal>(v);
    switch (op) {
      SH_FOR_EACH_OP(SH2_OP)
      default: return variant<Op::Illegal>(v);
    }
#undef SH2_OP
  }
};

// Decode the instruction word at `pc` into `c`.
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
  // External fetches update BSC bus history when they execute; cache hits
  // do not generate a bus transaction.
  const bool external = bus_.external(at);
  c->fetch = 0;
  const bool line_start = (pc & 3) == 0;

  if (is_delayed_branch(d.op)) {
    // Fuse the delay slot into the branch cell.
    const u32 slot_pc = pc + 2;
    const u8 slot_at = bus_.attr(slot_pc);
    const DecodedInsn sd = (slot_at & Bus::kNoExec) ? DecodedInsn{} : decode(bus_.read16(slot_pc));
    const bool bad = (slot_at & Bus::kNoExec) || sd.op == Op::Illegal || writes_pc(sd.op);
    if (bad) {
      c->fn = ExecImpl::select(d.op, external && line_start ? Var::BadLine : Var::Bad);
    } else {
      // Exactly one of the two words starts a fetch line.
      c->fn = ExecImpl::select(d.op, external ? Var::Line : Var::Plain);
      c->real = ExecImpl::select(sd.op, Var::Slot);
      c->slot_imm = sd.imm;
      c->slot_n = sd.n;
      c->slot_m = sd.m;
      c->slot_cyc = sd.cycles;
      if ((pc & (kPageSize - 1)) >= kPageSize - 4) c->flags |= kFlagPageEdge;
      bus_.mark_code(slot_pc);
    }
  } else {
    c->fn = ExecImpl::select(d.op, external && line_start ? Var::Line : Var::Plain);
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
  EMU_TAILCALL(c->fn(cpu, c));
}

// Guard cell after the last instruction of a page: continue in the next page.
const Cell* page_end(Cpu& cpu, const Cell*) {
  const Cell* nx = cpu.cells_for(cpu.page_pc_ + kPageSize);
  SH2_CHAIN(nx);
}

}  // namespace detail
}  // namespace sh2

#include "cpu/sh3/cpu.hpp"

#include <algorithm>
#include <cstring>

namespace sh3 {

namespace {
Cell blank_cell() {
  Cell c{};
  c.fn = &detail::fill;
  return c;
}
Cell guard_cell() {
  Cell c{};
  c.fn = &detail::page_end;
  return c;
}
}  // namespace

Cpu::Cpu(Bus& bus) : bus_(bus), cells_(32, blank_cell(), 1, guard_cell()) {
  bus_.set_code_sink(this);
}

Cpu::~Cpu() { bus_.set_code_sink(nullptr); }

void Cpu::reset() {
  regs_ = Regs{};
  std::memset(bank_, 0, sizeof bank_);
  expevt_ = kExcPowerOn;
  intevt_ = intevt2_ = 0;
  tra_ = 0;
  sleeping_ = false;
  irq_taken_ = false;
  pending_ = 0;
  budget_ = 0;
  retime_ = false;
  reeval_irq();
  cells_for(regs_.pc);  // branch_to() assumes a current page
}

void Cpu::manual_reset() {
  reset();
  expevt_ = kExcManualReset;
}

// SR writes: RB selects the register bank, IMASK / BL gate interrupts.
void Cpu::set_sr(u32 v) {
  v &= kSrMask;
  if ((v ^ regs_.sr) & kSrRB) switch_bank();
  regs_.sr = v;
  reeval_irq();
}

void Cpu::switch_bank() {
  for (unsigned i = 0; i < 8; ++i) std::swap(regs_.r[i], bank_[i]);
}

u32 Cpu::misaligned(u32 a, u32 mask, bool) {
  ++misaligned_count_;
  return a & ~mask;
}

// ---------------------------------------------------------------------------
// Cells

void Cpu::invalidate_all() { cells_.invalidate_all(); }

// A delayed branch carries its slot's decoding: the word before the range
// drops too.
void Cpu::invalidate_range(u32 addr, u32 len) { cells_.invalidate_range(addr, len, 1); }

// A write to a RAM line holding code: the line is aliased in every region
// of the physical space, so every alias of the address drops its cells.
void Cpu::code_line_written(u32 addr) {
  const u32 line = addr & ~(Bus::kLineSize - 1);
  if (addr < 0xE0000000u) {
    const u32 phys = line & Bus::kPhysMask;
    for (u32 region = 0; region < Bus::kRegions; ++region) invalidate_range(Bus::region_base(region) | phys, Bus::kLineSize);
  } else {
    invalidate_range(line, Bus::kLineSize);
  }
  bus_.unmark_code(addr);
}

size_t Cpu::cache_pages_allocated() const { return cells_.allocated_pages(); }

// ---------------------------------------------------------------------------
// Exceptions

void Cpu::enter_exception(u32 code, u32 save_pc, u32 vector_offset, bool interrupt) {
  ++exc_count_;
  regs_.spc = save_pc;
  regs_.ssr = regs_.sr;
  if (!interrupt) expevt_ = code;
  set_sr(regs_.sr | kSrMD | kSrRB | kSrBL);
  regs_.pc = regs_.vbr + vector_offset;
  sleeping_ = false;
  clear_pending(kPendSleep);
}

void Cpu::reeval_irq() {
  u32 code, code2;
  unsigned source;
  const int pri = intc_ ? intc_->pending_priority(code, code2, source) : -1;
  bool accept = false;
  if (pri == IntcBlock::kNmiPriority) accept = !(regs_.sr & kSrBL) || intc_->nmi_ignores_bl();
  else if (pri > 0) accept = !(regs_.sr & kSrBL) && pri > int(interrupt_mask());
  if (accept) raise(kPendIrq); else clear_pending(kPendIrq);
}

bool Cpu::take_interrupt() {
  u32 code, code2;
  unsigned source;
  const int pri = intc_ ? intc_->pending_priority(code, code2, source) : -1;
  if (pri < 0) return false;
  if (pri == IntcBlock::kNmiPriority) {
    if ((regs_.sr & kSrBL) && !intc_->nmi_ignores_bl()) return false;
  } else if ((regs_.sr & kSrBL) || pri <= int(interrupt_mask())) {
    return false;
  }
  irq_taken_ = true;
  intc_->acknowledge(source);
  intevt_ = code;
  intevt2_ = code2;
  enter_exception(code2, regs_.pc, kVecInterrupt, true);
  budget_ -= s32(kExcStates);
  reeval_irq();
  return true;
}

// At an instruction boundary (regs_.pc = next instruction) after a pending
// condition forced the run loop out.
void Cpu::service_pending() {
  clear_pending(kPendSleep);
  if (pending_ & kPendIrq) take_interrupt();
}

// ---------------------------------------------------------------------------
// Run loop

u64 Cpu::run_slice(s32 slice) {
  if (EMU_UNLIKELY(pending_ & kPendRefetch)) {
    clear_pending(kPendRefetch);
    if (retime_) { retime_ = false; invalidate_all(); }
    cells_for(regs_.pc);
  }
  reeval_irq();
  begin_slice(slice);
  if (sleeping_) {
    if (pending_ & kPendIrq) service_pending();
    else budget_ -= slice;
  } else {
    const Cell* ip = cells_for(regs_.pc);
    do {
      ip = ip->fn(*this, ip);
    } while (budget_ > 0);
    regs_.pc = pc_of(ip);
    if (pending_) service_pending();
  }
  cur_ip_ = nullptr;
  return end_slice();
}

unsigned Cpu::step() {
  irq_taken_ = false;
  if (sleeping_ && !(pending_ & kPendIrq)) {
    total_states_ += 1;
    return 1;
  }
  return unsigned(run_slice(0));
}

u64 Cpu::poll() {
  reeval_irq();
  if (!(pending_ & kPendIrq)) return 0;
  set_budget(0);
  service_pending();
  const u64 used = u64(-s64(true_budget()));
  total_states_ += used;
  return used;
}

u64 Cpu::run(u64 states) {
  irq_taken_ = false;
  u64 used = 0;
  while (used < states) {
    const s32 slice = s32(std::min<u64>(states - used, u64(kMaxSlice)));
    if (sleeping_ && !(pending_ & kPendIrq)) {
      reeval_irq();
      if (!(pending_ & kPendIrq)) {
        total_states_ += u64(slice);
        used += u64(slice);
        continue;
      }
    }
    used += run_slice(slice);
    if (take_cut()) break;
  }
  return used;
}

}  // namespace sh3

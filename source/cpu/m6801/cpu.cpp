#include "cpu/m6801/cpu.hpp"

#include <algorithm>

namespace m6801 {

namespace {
Cell blank_cell() {
  Cell c{};
  c.fn = &detail::cell_fill;
  c.len = 1;
  return c;
}
Cell cross_cell() {
  Cell c{};
  c.fn = &detail::cell_cross;
  c.len = 1;
  return c;
}
}  // namespace

Cpu::Cpu(Bus& bus, ChipModel model)
    : bus_(bus), model_(model), family_(family_of(model)), cells_(16, blank_cell(), kGuardCells, cross_cell()) {
  const bool hc11 = family_ == Family::HC11;
  swi_vector_ = hc11 ? hc11::kVecSwi : kVecSwi;
  trap_vector_ = hc11 ? hc11::kVecIllop : kVecTrap;
  bus_.set_code_sink(this);
}

Cpu::~Cpu() { bus_.set_code_sink(nullptr); }

void Cpu::reset() {
  cc_ = kCcrFixed | kI;  // the HC11's S and X sit where the fixed bits are
  waiting_ = sleeping_ = false;
  irq_taken_ = false;
  irq_ok_at_ = 0;
  pending_ = 0;
  budget_ = 0;
  invalidate_all();
  pc_ = bus_.read16(kVecReset);
  if (intc_) intc_->mask_set();
  reeval_irq();
}

void Cpu::set_mode(int mode) {
  // HD6301V1 manual table 2-13-1: in the expanded modes an instruction
  // fetched from $0000-$001F is an address error.
  if (family_ == Family::HD6301 && mode != 5 && mode != 7) { trap_lo_ = 0; trap_hi_ = kRegFieldSize - 1; }
  else { trap_lo_ = 1; trap_hi_ = 0; }
  invalidate_all();
}

void Cpu::set_ccr(u8 v) {
  cc_ = family_ == Family::HC11 ? v : u8(v | kCcrFixed);
  if ((cc_ & kI) && intc_) intc_->mask_set();
  reeval_irq();
}

DecodedInsn Cpu::decode_at(u16 addr) const {
  const u8* mem = bus_.mem();
  return decode(family_, addr, [mem, addr](unsigned i) { return mem[u16(addr + i)]; });
}

// ---------------------------------------------------------------------------
// Instruction cache

void Cpu::invalidate_all() {
  cells_.invalidate_all();
  page_pc_ = kNoPage;
  device_code_ = false;
}

void Cpu::invalidate_range(u32 addr, u32 len) {
  addr &= 0xFFFF;
  if (len > 0x10000) len = 0x10000;
  // An instruction starting up to four bytes before the range may extend
  // into it, across $FFFF too.
  constexpr u32 back = kGuardCells - 1;
  if (addr < back) cells_.invalidate_range(0x10000 - (back - addr), back - addr, 0);
  if (addr + len > 0x10000) {
    cells_.invalidate_range(addr, 0x10000 - addr, back);
    cells_.invalidate_range(0, addr + len - 0x10000, 0);
    return;
  }
  cells_.invalidate_range(addr, len, back);
}

void Cpu::select_code_bank(u32 base, u32 size, u32 key) {
  if (!cells_.select_bank(base, size, key)) return;
  if (in_slice_) raise(kPendRefetch);
  else page_pc_ = kNoPage;
}

void Cpu::code_line_written(u32 addr) {
  invalidate_range(addr & ~(Bus::kLineSize - 1), Bus::kLineSize);
  bus_.unmark_code(addr);
}

// ---------------------------------------------------------------------------
// Interrupts

void Cpu::push_state(u16 pc, unsigned cyc) {
  push8(u8(pc), cyc++);
  push8(u8(pc >> 8), cyc++);
  if (family_ == Family::HC11) {
    push8(u8(y_), cyc++);
    push8(u8(y_ >> 8), cyc++);
  }
  push8(u8(x_), cyc);
  push8(u8(x_ >> 8), cyc + 1);
  push8(a_, cyc + 2);
  push8(b_, cyc + 3);
  push8(cc_, cyc + 4);
}

void Cpu::reeval_irq() {
  if (intc_ && (intc_->nmi_pending() || (!(cc_ & kI) && intc_->maskable_pending()))) raise(kPendIrq);
  else clear_pending(kPendIrq);
}

// The interrupt sequence at an instruction boundary (or out of WAI, whose
// state is stacked already). A maskable request waits for the I bit and for
// the hold-off after the instruction that cleared it.
bool Cpu::take_interrupt() {
  if (!intc_) return false;
  if (!intc_->nmi_pending()) {
    if ((cc_ & kI) || !intc_->maskable_pending()) return false;
    if (total_states() < irq_ok_at_) return false;
  }
  const EntryTiming t = entry_timing(family_);
  unsigned cycles = t.cycles, vector_cycle = t.vector;
  if (waiting_) {
    waiting_ = false;
    clear_pending(kPendSleep);
    cycles = kWaiExitCycles;
    vector_cycle = kWaiExitCycles - 3;
  } else {
    push_state(pc_, t.push);
  }
  // The vector is selected before the I bit clears the maskable flip-flops.
  const u16 vector = intc_->acknowledge();
  if (family_ == Family::HC11 && vector == hc11::kVecXirq) cc_ |= kX;
  set_i();
  pc_ = rd16(vector, vector_cycle);
  budget_ -= s32(cycles);
  irq_taken_ = true;
  ++interrupt_count_;
  reeval_irq();
  return true;
}

// Waiting or sleeping at a boundary: whether the CPU runs again.
bool Cpu::wake() {
  if (waiting_) return take_interrupt();
  if (!intc_ || !intc_->sleep_release()) return false;
  // A request ends SLP even while masked; only an accepted one is serviced.
  sleeping_ = false;
  clear_pending(kPendSleep);
  take_interrupt();
  return true;
}

// ---------------------------------------------------------------------------
// Execution

u64 Cpu::run_slice(s32 slice) {
  if (EMU_UNLIKELY(pending_ & kPendRefetch)) {
    clear_pending(kPendRefetch);
    page_pc_ = kNoPage;
  }
  begin_slice(slice);
  if (EMU_UNLIKELY(waiting_ || sleeping_)) {
    if (!wake()) {
      budget_ -= std::max(slice, 1);
      return end_slice();
    }
  } else if (pending_ & kPendIrq) {
    take_interrupt();
  }
  // A held-off request keeps kPendIrq raised: every instruction then ends
  // the slice, and the boundary after it is checked again.
  const Cell* ip = cell_at(pc_);
  do {
    ip = ip->fn(*this, ip);
  } while (budget_ > 0);
  pc_ = pc_of(ip);
  return end_slice();
}

unsigned Cpu::step() {
  irq_taken_ = false;
  return unsigned(run_slice(0));
}

u64 Cpu::poll() {
  if (!(pending_ & kPendIrq) && !waiting_ && !sleeping_) return 0;
  begin_slice(0);
  if (waiting_ || sleeping_) wake();
  else take_interrupt();
  return end_slice();
}

u64 Cpu::run(u64 states) {
  irq_taken_ = false;
  u64 used = 0;
  while (used < states) {
    const s32 slice = s32(std::min<u64>(states - used, u64(kMaxSlice)));
    used += run_slice(slice);
    if (take_cut()) break;
  }
  return used;
}

}  // namespace m6801

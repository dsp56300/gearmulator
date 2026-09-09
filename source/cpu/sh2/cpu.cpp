#include "cpu/sh2/cpu.hpp"

#include <cstdio>
#include <cstdlib>

#include <algorithm>

namespace sh2 {

Cpu::Cpu(Bus& bus, const ChipConfig& cfg)
    : bus_(bus), cfg_(cfg), addr_mask_(cfg.addr_mask), pages_(1u << (32 - kPageShift)) {
  bus_.set_code_sink(this);
}

Cpu::~Cpu() { bus_.set_code_sink(nullptr); }

void Cpu::reset() {
  regs_ = Regs{};
  regs_.sr = kIMask;  // I3-I0 = 1111, everything else 0
  regs_.vbr = 0;
  regs_.pc = bus_.read32(0);
  regs_.r[15] = bus_.read32(4);
  sleeping_ = standby_ = false;
  irq_taken_ = false;
  pending_ = 0;
  budget_ = 0;
  ccr_ = 0;
  cache_miss_last_ = false;
  cache_tags_.fill(0);
  reeval_irq();
  cells_for(regs_.pc);  // branch_to() assumes a current page
}

void Cpu::set_cache_control(u8 ccr) {
  const u8 enabled = u8(ccr & 0x1F);
  if (enabled == ccr_) return;
  if (enabled & ~ccr_) cache_miss_last_ = false;
  ccr_ = enabled;
  invalidate_all();  // cells bake in whether their line goes through the cache
}

// ---------------------------------------------------------------------------
// Cells

Cell* Cpu::alloc_page(u32 page) {
  pages_[page].reset(new Cell[kCellsPerPage + 1]);
  reset_cells(page, 0, kCellsPerPage);
  Cell& guard = pages_[page][kCellsPerPage];
  guard = Cell{};
  guard.fn = &detail::page_end;
  return pages_[page].get();
}

void Cpu::reset_cells(u32 page, u32 first, u32 count) {
  Cell* cells = pages_[page].get();
  if (!cells) return;
  Cell blank{};
  blank.fn = &detail::fill;
  const u32 end = std::min<u32>(first + count, kCellsPerPage);
  for (u32 i = first; i < end; ++i) cells[i] = blank;
}

void Cpu::invalidate_all() {
  for (u32 p = 0; p < pages_.size(); ++p) reset_cells(p, 0, kCellsPerPage);
}

void Cpu::invalidate_range(u32 addr, u32 len) {
  // A delayed branch carries its slot's decoding: drop the word before too.
  if (addr >= 2) {
    addr -= 2;
    len += 2;
  }
  const u32 first_page = addr >> kPageShift, last_page = (addr + len - 1) >> kPageShift;
  for (u32 p = first_page; p <= last_page; ++p) {
    const u32 lo = p == first_page ? (addr & (kPageSize - 1)) >> 1 : 0;
    const u32 hi = p == last_page ? ((addr + len - 1) & (kPageSize - 1)) >> 1 : kCellsPerPage - 1;
    reset_cells(p, lo, hi - lo + 1);
  }
}

void Cpu::code_line_written(u32 addr) {
  invalidate_range(addr & ~(Bus::kLineSize - 1), Bus::kLineSize);
  bus_.unmark_code(addr);
}

size_t Cpu::cache_pages_allocated() const {
  size_t n = 0;
  for (const auto& p : pages_) n += p ? 1 : 0;
  return n;
}

// ---------------------------------------------------------------------------
// Exceptions

namespace {
}

void Cpu::enter_exception(u8 vector, u32 push_pc, int new_mask) {
  ++exc_count_;
  push32(regs_.sr);
  push32(push_pc);
  if (new_mask >= 0) regs_.sr = (regs_.sr & ~kIMask) | (u32(new_mask) << kIShift);
  regs_.pc = read32(regs_.vbr + u32(vector) * 4);
  reeval_irq();
}

// At an instruction boundary (regs_.pc = next instruction) after a pending
// condition forced the run loop out.  Priority per table 5.1.
void Cpu::service_pending() {
  clear_pending(kPendSleep);
  if (pending_ & kPendAddrErr) {
    clear_pending(kPendAddrErr);
    enter_exception(kVecCpuAddrErr, regs_.pc, -1);
    budget_ -= s32(kExcStates);
    return;
  }
  if (pending_ & kPendDmaAddrErr) {
    clear_pending(kPendDmaAddrErr);
    sleeping_ = false;
    enter_exception(kVecDmaAddrErr, regs_.pc, -1);
    budget_ -= s32(kExcStates);
    return;
  }
  if (pending_ & kPendDefer) {
    // The instruction after an interrupt-disabling instruction always runs
    // (table 5.9): skip this boundary only.
    clear_pending(kPendDefer);
    return;
  }
  if (pending_ & kPendNmi) {
    clear_pending(kPendNmi);
    sleeping_ = standby_ = false;
    irq_taken_ = true;
    enter_exception(kVecNmi, regs_.pc, 15);
    budget_ -= s32(kIrqStates);
    return;
  }
  if ((pending_ & kPendIrq) && !standby_) {
    sleeping_ = false;
    irq_taken_ = true;
    const u8 vector = irq_vector_;
    enter_exception(vector, regs_.pc, irq_level_);  // mask := level drops the request
    budget_ -= s32(kIrqStates + (irq_pin_ ? kIrqPinExtra : 0));
    if (irq_ack_) irq_ack_->irq_acknowledged(vector);
    return;
  }
}

// ---------------------------------------------------------------------------
// Run loop

u64 Cpu::run_slice(s32 slice) {
  reeval_irq();
  set_budget(slice);
  slice_len_ = slice;
  in_slice_ = true;
  if (sleeping_) {
    if (pending_ & wake_mask()) service_pending();
    else budget_ -= slice;
  } else {
    const Cell* ip = cells_for(regs_.pc);
    // A slice ends at the first instruction boundary at or after its target
    // (budget <= 0), so an event due exactly on a boundary runs there.
    do {
      ip = ip->fn(*this, ip);
    } while (budget_ > 0);
    regs_.pc = pc_of(ip);
    if (pending_) service_pending();
  }
  const s64 used = s64(slice_len_) - s64(true_budget());  // slice_len_ shrinks in cut_slice()
  in_slice_ = false;
  total_states_ += u64(used);
  return u64(used);
}

unsigned Cpu::step() {
  irq_taken_ = false;
  if (sleeping_ && !(pending_ & wake_mask())) {
    total_states_ += 1;
    return 1;
  }
  return unsigned(run_slice(0));
}

u64 Cpu::poll() {
  reeval_irq();
  if (!(pending_ & (kPendAddrErr | kPendDmaAddrErr | kPendDefer | kPendNmi | kPendIrq))) return 0;
  if (sleeping_ && !(pending_ & wake_mask())) return 0;
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
    if (sleeping_ && !(pending_ & wake_mask())) {
      total_states_ += u64(slice);
      used += u64(slice);
      continue;
    }
    used += run_slice(slice);
    if (cut_) {
      cut_ = false;
      break;
    }
  }
  return used;
}

}  // namespace sh2

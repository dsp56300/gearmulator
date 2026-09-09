#include "cpu/h8500/cpu.hpp"

#include <algorithm>

namespace h8500 {

Cpu::Cpu(Bus& bus, const ChipConfig& cfg) : bus_(bus), cfg_(cfg) {
  max_mode_ = cfg_.max_mode();
  bus_.set_code_sink(this);
}

Cpu::~Cpu() { bus_.set_code_sink(nullptr); }

void Cpu::reset() {
  max_mode_ = cfg_.max_mode();
  regs_.sr = u16((regs_.sr & ~kT & kSrMask) | kMaskBits);  // T = 0, I2-I0 = 7
  sleeping_ = false;
  irq_taken_ = false;
  pending_ = 0;
  budget_ = 0;
  if (max_mode_) {
    regs_.cp = bus_.read8(1);
    regs_.pc = bus_.read16(2);
  } else {
    regs_.pc = bus_.read16(0);
  }
  reeval_irq();
}

DecodedInsn Cpu::decode_at(u32 addr) const {
  const u32 page = addr & 0xFF0000u;
  const u16 off = u16(addr);
  return decode([&](unsigned i) { return bus_.read8(max_mode_ ? (page | u16(off + i)) : u16(off + i)); });
}

// ---------------------------------------------------------------------------
// Instruction cache storage

Cell* Cpu::alloc_page(unsigned page) {
  pages_[page].reset(new Cell[kCellsPerPage]);
  reset_cells(page, 0, kCellsPerPage);
  return pages_[page].get();
}

void Cpu::reset_cells(unsigned page, u32 first, u32 count) {
  Cell* cells = pages_[page].get();
  if (!cells) return;
  Cell blank{};
  blank.fn = &detail::cell_fill;
  blank.x = 1;
  const u32 end = std::min<u32>(first + count, kCellsPerPage);
  for (u32 i = first; i < end; ++i) cells[i] = blank;
}

void Cpu::invalidate_all() {
  for (unsigned p = 0; p < 256; ++p) reset_cells(p, 0, kCellsPerPage);
}

void Cpu::invalidate_range(u32 addr, u32 len) {
  const unsigned page = max_mode_ ? ((addr >> 16) & 0xFF) : 0;
  // An instruction starting up to 5 bytes before the range may extend into it.
  const u32 first = addr & 0xFFFF;
  const u32 back = std::min<u32>(first, 5);
  reset_cells(page, first - back, len + back);
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
// Control registers

// Table 1-12 documents SR as word-only and CCR/BR/EP/DP/TP as byte-only, the
// other combinations being "not allowed".  Firmware uses the word forms of
// the byte registers anyway (LDC.W #xx:16,DP; STC.W/LDC.W EP round trips
// through the stack), so they are given the behaviour observed on hardware
// by other emulators: a byte register reads duplicated into both halves and
// is written from the low byte, except EP whose word form is the EP:DP pair.
u32 Cpu::read_cr(u8 cr, Size sz) const {
  if (sz == Size::Word) {
    switch (cr) {
      case 0: case 1: return regs_.sr;
      case 3: return u32(regs_.br) * 0x0101u;
      case 4: return (u32(regs_.ep) << 8) | regs_.dp;
      case 5: return u32(regs_.dp) * 0x0101u;
      case 7: return u32(regs_.tp) * 0x0101u;
      default: return 0;
    }
  }
  switch (cr) {
    case 0: case 1: return regs_.sr & 0xFF;
    case 3: return regs_.br;
    case 4: return regs_.ep;
    case 5: return regs_.dp;
    case 7: return regs_.tp;
    default: return 0;
  }
}

void Cpu::set_sr(u16 v) {
  const u16 old = regs_.sr;
  regs_.sr = u16(v & kSrMask);
  if ((old ^ regs_.sr) & kMaskBits) mask_changed_ = true;
  reeval_irq();
  reeval_trace();
}

void Cpu::write_cr(u8 cr, u32 v, Size sz) {
  if (sz == Size::Word) {
    switch (cr) {
      case 0: case 1: set_sr(u16(v)); break;
      case 3: regs_.br = u8(v); break;
      case 4: regs_.ep = u8(v >> 8); regs_.dp = u8(v); break;
      case 5: regs_.dp = u8(v); break;
      case 7: regs_.tp = u8(v); break;
      default: break;
    }
    return;
  }
  switch (cr) {
    case 0: case 1: regs_.sr = u16((regs_.sr & 0xFF00) | (v & kCcrMask)); break;
    case 3: regs_.br = u8(v); break;
    case 4: regs_.ep = u8(v); break;
    case 5: regs_.dp = u8(v); break;
    case 7: regs_.tp = u8(v); break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
// Exceptions

void Cpu::enter_exception(u8 vector, u16 push_pc, int new_mask) {
  ++exc_count_;
  if (new_mask < 0) ++fault_count_;  // interrupts (and NMI) carry a new mask
  push16(push_pc);
  if (max_mode_) push16(regs_.cp);  // upper byte "don't care"
  push16(regs_.sr);
  regs_.sr = u16(regs_.sr & ~kT);
  if (new_mask >= 0) regs_.sr = u16((regs_.sr & ~kMaskBits) | (u16(new_mask) << kMaskShift));
  if (max_mode_) {
    const u32 va = u32(vector) * 4;
    regs_.cp = bus_.read8(va + 1);
    regs_.pc = bus_.read16(va + 2);
  } else {
    regs_.pc = bus_.read16(u32(vector) * 2);
  }
  sleeping_ = false;
  reeval_irq();
  reeval_trace();
}

// Called at an instruction boundary (regs_.pc = next instruction) when a
// pending condition forced the run loop to exit.  Priority per Table 3-1.
void Cpu::service_pending() {
  clear_pending(kPendSleep);  // the sleeping_ flag carries the state
  clear_pending(kPendBreak);  // the boundary itself is the service
  if (pending_ & kPendAddrErr) {
    clear_pending(kPendAddrErr);
    enter_exception(kVecAddressError, regs_.pc, -1);
    budget_ -= s32(exception_states());
    return;
  }
  if (pending_ & kPendDefer) {
    // The instruction after LDC/ANDC/ORC/XORC/RTE always executes: skip this
    // boundary.  Other pending bits keep the budget forced, so the next
    // instruction runs alone and we get back here.
    clear_pending(kPendDefer);
    // The new mask is effective from the third state after the writing
    // instruction: boundaries before that (a shorter successor, or a slice
    // boundary falling in between) do not let an interrupt through.
    if (mask_changed_) mask_effective_at_ = total_states() + 3;
    mask_changed_ = false;
    deferred_boundary_ = total_states();
    return;
  }
  // A poll() at the boundary a deferral just skipped must not take anything
  // either: the next instruction has not run yet.
  if (total_states() == deferred_boundary_) return;
  if (total_states() < mask_effective_at_) return;
  if (pending_ & kPendTrace) {
    // Trace exception at the completion of every instruction while T = 1.
    // Entry clears T (and thereby the pending bit); the handler's first
    // instruction is not traced.
    enter_exception(kVecTrace, regs_.pc, -1);
    budget_ -= s32(exception_states());
    return;
  }
  if (pending_ & kPendNmi) {
    clear_pending(kPendNmi);
    irq_taken_ = true;
    enter_exception(kVecNmi, regs_.pc, 7);
    budget_ -= s32(irq_states());
    return;
  }
  if (pending_ & kPendIrq) {
    irq_taken_ = true;
    const u8 vector = irq_vector_;
    enter_exception(vector, regs_.pc, irq_level_);  // mask := level clears the bit
    budget_ -= s32(irq_states());
    if (irq_ack_) irq_ack_->irq_acknowledged(vector);  // controller may present the next request
    return;
  }
}

// ---------------------------------------------------------------------------
// Run loop

u64 Cpu::run_slice(s32 slice) {
  // Pick up direct edits of SR (tests / debuggers) and re-check the IRQ line.
  reeval_trace();
  reeval_irq();
  set_budget(slice);
  slice_len_ = slice;
  in_slice_ = true;
  if (sleeping_) {
    const u64 now = total_states();
    if (!(pending_ & (kPendNmi | kPendIrq))) {
      budget_ -= slice;  // idle through the slice
    } else if (now < mask_effective_at_) {
      // The wake-up waits for the new mask to take effect.
      budget_ -= s32(std::max<u64>(1, std::min<u64>(std::max<u64>(u64(slice), 1), mask_effective_at_ - now)));
    } else {
      service_pending();  // wakes up
    }
  } else {
    const Cell* ip = cells_for(regs_.cp, regs_.pc);
    // A slice ends at the first instruction boundary at or after its target
    // (budget <= 0), so an event due exactly on a boundary runs there.
    do {
      ip = ip->fn(*this, ip);
    } while (budget_ > 0);
    regs_.pc = u16(ip - page_cells_);
    if (pending_) service_pending();
  }
  const s64 used = s64(slice_len_) - s64(true_budget());  // slice_len_ shrinks in cut_slice()
  in_slice_ = false;
  total_states_ += u64(used);
  return u64(used);
}

unsigned Cpu::step() {
  irq_taken_ = false;
  if (sleeping_ && !(pending_ & (kPendNmi | kPendIrq))) {
    total_states_ += kSleepIdleStates;
    return kSleepIdleStates;
  }
  return unsigned(run_slice(0));
}

u64 Cpu::poll() {
  reeval_trace();
  reeval_irq();
  if (!(pending_ & (kPendAddrErr | kPendDefer | kPendTrace | kPendNmi | kPendIrq))) return 0;
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
    if (sleeping_ && !(pending_ & (kPendNmi | kPendIrq))) {
      // Nothing can happen until an interrupt arrives: idle the whole request.
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

}  // namespace h8500

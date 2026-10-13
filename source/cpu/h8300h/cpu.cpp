#include "cpu/h8300h/cpu.hpp"

#include <algorithm>
#include <cstring>

namespace h8300h {

namespace {
Cell blank_cell() {
  Cell c{};
  c.fn = &detail::cell_fill;
  c.len = 2;
  return c;
}
Cell cross_cell() {
  Cell c{};
  c.fn = &detail::cell_cross;
  c.len = 2;
  return c;
}
}  // namespace

Cpu::Cpu(Bus& bus) : bus_(bus), cells_(24, blank_cell(), kGuardCells, cross_cell()) {
  bus_.set_code_sink(this);
}

Cpu::~Cpu() { bus_.set_code_sink(nullptr); }

void Cpu::reset() {
  std::memset(er_, 0, sizeof er_);
  ccr_ = kCcrI;
  acc_ = 0;
  holdoff_ = false;
  sleeping_ = standby_ = false;
  faulted_ = false;
  fault_addr_ = 0;
  irq_taken_ = false;
  pending_ = 0;
  budget_ = 0;
  retime_ = false;
  retime_classes_ = 0;
  invalidate_all();  // the bus controller is back at its reset timing
  pc_ = bus_.read32(0) & kAddrMask;
  reeval_irq();
}

DecodedInsn Cpu::decode_at(u32 addr) const {
  const u8* mem = bus_.mem();
  return decode([mem, addr](unsigned i) { return mem[(addr + i) & kAddrMask]; });
}

// Manual 7.2: internal RAM two states per word, the register field three
// states per byte (per word for the 16-bit registers), external areas two
// or three states per bus cycle plus the programmed waits, one cycle per
// byte on an 8-bit area.
u32 Cpu::access_states(u32 a, unsigned bytes) const {
  const u8 at = bus_.attr(a & kAddrMask);
  if (EMU_LIKELY((at & Bus::kClassMask) != kClsRegs)) return bus_.access_class_of(at).cycles(bytes);
  const unsigned low = a & 0xFF;
  if (low < 0x10) return 2 * ((bytes + 1) / 2);  // the RAM tail in the register field's line
  bool word = bytes >= 2 && !(a & 1);
  if (word) {
    word = (low >= 0x20 && low < 0x60) || (low >= 0x68 && low <= 0x9E && !(low & 1)) || (low >= 0xE0 && low <= 0xE6);
  }
  return 3 * (word ? (bytes + 1) / 2 : bytes);
}

u32 Cpu::fetch_states(u32 pc, unsigned words) const {
  u32 st = 0;
  for (unsigned i = 0; i < words; ++i) st += access_states((pc + 2 * i) & kAddrMask, 2);
  return st;
}

// ---------------------------------------------------------------------------
// Instruction cache

void Cpu::invalidate_all() {
  cells_.invalidate_all();
  page_pc_ = kNoPage;
}

void Cpu::invalidate_range(u32 addr, u32 len) {
  // An instruction starting up to eight bytes before the range may extend into it.
  cells_.invalidate_range(addr & kAddrMask, len, 4);
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
// Exceptions

u32 Cpu::enter_exception(unsigned vector) {
  const u32 before = acc_;
  push32((pc_ & kAddrMask) | (u32(ccr_) << 24));
  ccr_ |= kCcrI;
  if (intc_ && !(intc_->syscr() & 0x08)) ccr_ |= kCcrUi;
  pc_ = rd32(vector * 4) & kAddrMask;
  sleeping_ = standby_ = false;
  clear_pending(kPendSleep);
  const u32 used = acc_ - before + 4;
  acc_ = before;
  return used;
}

void Cpu::reeval_irq() {
  unsigned which;
  if (intc_ && intc_->arbitrate(which, ccr_)) raise(kPendIrq);
  else clear_pending(kPendIrq);
}

bool Cpu::take_interrupt() {
  unsigned which;
  if (!intc_ || !intc_->arbitrate(which, ccr_)) return false;
  irq_taken_ = true;
  intc_->acknowledge(which);
  budget_ -= s32(enter_exception(which));
  reeval_irq();
  return true;
}

// ---------------------------------------------------------------------------
// Execution

u64 Cpu::run_slice(s32 slice) {
  if (EMU_UNLIKELY(pending_ & kPendRefetch)) {
    clear_pending(kPendRefetch);
    if (retime_) { retime_ = false; retime_classes_ = 0; cells_.invalidate_all(); }
    else if (retime_classes_) {
      const u16 classes = retime_classes_;
      retime_classes_ = 0;
      // A page holds stale cells if any line it fetches from, including the first line of the
      // next page that its last instructions extend into, is in a retimed class.
      cells_.invalidate_pages_if([&](u32 base) {
        for (u32 a = base; a <= base + CellPages::kPageSize && a <= kAddrMask; a += Bus::kLineSize)
          if ((classes >> (bus_.attr(a) & Bus::kClassMask)) & 1) return true;
        return false;
      });
    }
    page_pc_ = kNoPage;
  }
  begin_slice(slice);
  if (EMU_UNLIKELY(faulted_ || (pending_ & kPendReset))) {
    // Stuck on the undefined instruction; time passes.
    budget_ -= std::max(slice, 1);
    return end_slice();
  }
  if (EMU_UNLIKELY(sleeping_ || standby_)) {
    unsigned which;
    const bool wake = intc_ && (standby_ ? intc_->standby_wake(ccr_) : intc_->arbitrate(which, ccr_));
    if (!wake || holdoff_) {
      holdoff_ = false;
      budget_ -= std::max(slice, 1);
      return end_slice();
    }
    if (standby_) {
      // Manual 20.4: the oscillator settling time selected by SYSCR.STS.
      static constexpr u32 waits[8] = {8192, 16384, 32768, 65536, 131072, 131072, 0, 0};
      budget_ -= s32(waits[(intc_->syscr() >> 4) & 7]);
      standby_ = false;
    }
    sleeping_ = false;
    clear_pending(kPendSleep);
    take_interrupt();
  } else {
    // A request is recognised at the boundary before an instruction, unless
    // the previous instruction (a CCR write, RTE) holds interrupts off for
    // one more.  That instruction then runs on its own, so the boundary
    // after it is checked whatever the slice length.
    if (!holdoff_ && (pending_ & kPendIrq)) take_interrupt();
  }
  if (holdoff_) {
    holdoff_ = false;
    raise(kPendHoldoff);  // one instruction, then a boundary
  }
  const Cell* ip = cell_at(pc_);
  do {
    ip = ip->fn(*this, ip);
  } while (budget_ > 0);
  pc_ = pc_of(ip);
  clear_pending(kPendHoldoff);
  return end_slice();
}

unsigned Cpu::step() {
  irq_taken_ = false;
  if (sleeping_ || standby_ || faulted_) {
    unsigned which;
    const bool wake = !faulted_ && intc_ && (standby_ ? intc_->standby_wake(ccr_) : intc_->arbitrate(which, ccr_));
    if (!wake) {
      total_states_ += 1;
      return 1;
    }
  }
  return unsigned(run_slice(0));
}

u64 Cpu::poll() {
  if (holdoff_ || sleeping_ || standby_ || faulted_ || !(pending_ & kPendIrq)) return 0;
  begin_slice(0);
  take_interrupt();
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

}  // namespace h8300h

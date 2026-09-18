#include "cpu/mcs96/cpu.hpp"

#include <algorithm>
#include <cstring>

namespace mcs96 {

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

Cpu::Cpu(Bus& bus, Variant variant) : bus_(bus), variant_(variant), cells_(16, blank_cell(), kGuardCells, cross_cell()) {
  bus_.set_code_sink(this);
  std::memset(ram_, 0xFF, sizeof ram_);
}

Cpu::~Cpu() { bus_.set_code_sink(nullptr); }

void Cpu::reset_architectural_state() {
  regs_ = Regs{};
  // The complete register file, including the general register RAM and the
  // stack pointer, comes out of reset high on both supported families.
  std::memset(ram_, 0xFF, sizeof ram_);
  holdoff_ = false;
  idle_ = false;
  power_down_ = false;
}

void Cpu::reset() {
  reset_architectural_state();
  pending_ = 0;
  budget_ = 0;
  if (sfr_) sfr_->reset_sfrs();
  reeval_irq();
}

u8 Cpu::slow_reg8(u8 a) const {
  if (a < 2) return 0;
  if (a == 0x08) return u8(regs_.psw);
  if (a < kSfrEnd) return sfr_ ? sfr_->read_sfr(a) : u8(0xFF);
  return ram_[a - kRegisterRamBase];
}

void Cpu::slow_set_reg8(u8 a, u8 v) {
  if (a < 2) return;
  if (a == 0x08) { set_psw(u16((regs_.psw & kFlagBits) | v)); return; }
  if (a < kSfrEnd) { if (sfr_) sfr_->write_sfr(a, v); return; }
  ram_[a - kRegisterRamBase] = v;
}

void Cpu::set_psw(u16 v) {
  regs_.psw = u16(v & ~kX);
  reeval_irq();
}

void Cpu::reeval_irq() {
  SfrBlock::Interrupt irq;
  if (sfr_ && sfr_->arbitrate(irq) && (!irq.maskable || (regs_.psw & kI))) raise(kPendIrq);
  else clear_pending(kPendIrq);
}

void Cpu::wake() {
  const bool was_down = power_down_;
  idle_ = false;
  power_down_ = false;
  clear_pending(kPendSleep);
  if (was_down && sfr_) sfr_->on_power_down(false);
}

void Cpu::sleep(bool power_down) {
  idle_ = true;
  raise(kPendSleep);
  if (power_down && !power_down_) {
    power_down_ = true;
    if (sfr_) sfr_->on_power_down(true);
  }
}

DecodedInsn Cpu::decode_at(u16 addr) const {
  const DecodedInsn d = decode([&](unsigned i) { return bus_.read8(u16(addr + i)); }, variant_);
  return d;
}

// ---------------------------------------------------------------------------
// Instruction cache storage

void Cpu::invalidate_all() { cells_.invalidate_all(); }


void Cpu::invalidate_range(u32 addr, u32 len) {
  // An instruction starting up to 6 bytes before the range may extend into it.
  cells_.invalidate_range(addr & 0xFFFF, len, 6);
}


void Cpu::select_code_bank(u32 base, u32 size, u32 key) {
  if (!cells_.select_bank(base, size, key)) return;
  // The current page pointer may now refer to the bank moved out; it stays
  // valid for the instruction in flight (its pc_of), and the next slice
  // re-resolves through the new bank.
  if (in_slice_) raise(kPendRefetch);
  else page_pc_ = kNoPage;
}


void Cpu::code_line_written(u32 addr) {
  invalidate_range(addr & ~(Bus::kLineSize - 1), Bus::kLineSize);
  bus_.unmark_code(addr);
}

// ---------------------------------------------------------------------------
// Interrupts

unsigned Cpu::enter_interrupt(const SfrBlock::Interrupt& irq) {
  push16(regs_.pc);
  const timing::DataSpace stack = external(sp()) ? timing::DataSpace::MemoryController : timing::DataSpace::Internal;
  regs_.pc = irq.indirect ? read16(irq.vector) : irq.vector;
  // Hardware guarantees execution of the first ISR instruction before a
  // second interrupt call can be acknowledged.
  hold_off();
  idle_ = false;
  if (!power_down_) clear_pending(kPendSleep);
  return timing::interruptEntry(variant_, stack);
}

bool Cpu::take_interrupt() {
  if (!sfr_) return false;
  SfrBlock::Interrupt irq;
  if (!sfr_->arbitrate(irq) || (irq.maskable && !(regs_.psw & kI))) return false;
  sfr_->acknowledge(irq);
  budget_ -= s32(enter_interrupt(irq));
  reeval_irq();
  return true;
}

// ---------------------------------------------------------------------------
// Run loop

u64 Cpu::run_slice(s32 slice) {
  if (EMU_UNLIKELY(pending_ & kPendRefetch)) {
    clear_pending(kPendRefetch);
    page_pc_ = kNoPage;
  }
  reeval_irq();
  begin_slice(slice);
  if (power_down_ || idle_) {
    // Idle: the core is frozen until an acceptable interrupt (or a wake-up
    // pin) arrives; time still passes for the peripherals.
    if (pending_ & kPendIrq) {
      // An accepted interrupt leaves idle; power-down needs a wake-up pin.
      if (!holdoff_) take_interrupt();
      else { holdoff_ = false; budget_ -= 1; }
    } else {
      budget_ -= std::max(slice, 1);
    }
    return end_slice();
  }
  // A request is recognised at the boundary before an instruction, unless
  // the previous instruction (or the interrupt entry) holds interrupts off
  // for one more.  That instruction then runs on its own, so the boundary
  // after it is checked whatever the slice length.
  bool serviced = false;
  if (!holdoff_ && (pending_ & kPendIrq)) serviced = take_interrupt();
  if (!serviced || budget_ > 0) {
    if (holdoff_) {
      holdoff_ = false;
      raise(kPendHoldoff);  // one instruction, then a boundary
    }
    const Cell* ip = cell_at(regs_.pc);
    do {
      ip = ip->fn(*this, ip);
    } while (budget_ > 0);
    regs_.pc = pc_of(ip);
  }
  clear_pending(kPendHoldoff);
  return end_slice();
}

unsigned Cpu::step() {
  if (power_down_ || idle_) {
    // A frozen core still hands the caller one state so a board can keep
    // sampling its wake-up pins.
    reeval_irq();
    if (!(pending_ & kPendIrq)) {
      total_states_ += 1;
      return 1;
    }
  }
  return unsigned(run_slice(0));
}

u64 Cpu::poll() {
  reeval_irq();
  if (holdoff_ || !(pending_ & kPendIrq)) return 0;
  begin_slice(0);
  take_interrupt();
  return end_slice();
}

u64 Cpu::run(u64 states) {
  u64 used = 0;
  while (used < states) {
    const s32 slice = s32(std::min<u64>(states - used, u64(kMaxSlice)));
    used += run_slice(slice);
    if (take_cut()) break;
  }
  return used;
}

}  // namespace mcs96

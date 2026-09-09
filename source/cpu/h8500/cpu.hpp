// H8/500 CPU core: register state, exception handling and the execution loop.
//
// Execution runs entirely from the instruction cache (icache.hpp).  Time is
// accounted in "states" (phi clock cycles).  The core is device independent;
// everything device specific (memory map, peripherals, interrupt controller)
// lives outside and talks to the CPU via the Bus and the interrupt interface.
//
// Budget / pending fold
// ---------------------
// run_slice(n) executes instructions until at least n states have elapsed.
// Each handler subtracts its state count from `budget_` and exits when it goes
// negative.  Anything that needs attention at an instruction boundary (address
// error, trace, NMI, IRQ, deferral after LDC/RTE, SLEEP) sets a bit in
// `pending_` and, the first time, also subtracts kForce from the budget, so the
// per-instruction check stays a single decrement-and-branch.  The true budget
// is recovered by adding kForce back.
#pragma once
#include <functional>
#include <memory>

#include "cpu/h8500/bus.hpp"
#include "cpu/h8500/chip.hpp"
#include "cpu/h8500/decode.hpp"
#include "cpu/h8500/icache.hpp"
#include "common/sched.hpp"
#include "cpu/h8500/timing.hpp"

#if defined(__GNUC__) || defined(__clang__)
#define H8_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define H8_LIKELY(x) __builtin_expect(!!(x), 1)
#else
#define H8_UNLIKELY(x) (x)
#define H8_LIKELY(x) (x)
#endif

namespace h8500 {

// Notified when the CPU starts the exception sequence for an IRQ, so the
// interrupt controller can drop edge-latched requests.
class IrqAckSink {
 public:
  virtual ~IrqAckSink() = default;
  virtual void irq_acknowledged(u8 vector) = 0;
};

class Cpu final : public CodeSink, public emu::Clock {
 public:
  // Status register (SR) layout.  Bits 15 (T), 10-8 (I2-I0) and 3-0 (N Z V C)
  // are implemented; all other bits read as 0.  The low byte is the CCR.
  enum : u16 {
    kC = 1u << 0,
    kV = 1u << 1,
    kZ = 1u << 2,
    kN = 1u << 3,
    kCcrMask = 0x000F,
    kMaskShift = 8,
    kMaskBits = 0x0700,
    kT = 1u << 15,
    kSrMask = 0x870F,
  };

  // Exception vector numbers.  Vector address = number * 2 (minimum mode) or
  // number * 4 (maximum mode, page 0).  Table 4-2 of the H8/510 manual.
  enum Vector : u8 {
    kVecReset = 0,
    kVecInvalidInsn = 2,
    kVecZeroDivide = 3,
    kVecTrapVs = 4,
    kVecAddressError = 8,
    kVecTrace = 9,
    kVecNmi = 11,
    kVecTrapaBase = 16,  // TRAPA #0..#15 -> 16..31
    kVecIrqBase = 32,    // external / internal interrupts (device specific)
  };

  struct Regs {
    u16 r[8] = {};   // R0-R7 (R6 = FP, R7 = SP)
    u16 pc = 0;      // valid between run()/step() calls (implicit in the cell pointer while running)
    u16 sr = kMaskBits;
    u8 cp = 0;       // code page
    u8 dp = 0;       // data page
    u8 ep = 0;       // extended page
    u8 tp = 0;       // stack page
    u8 br = 0;       // base register (@aa:8 upper byte)
  };

  // Pending-condition bits (see header comment).
  enum : u32 {
    kPendAddrErr = 1u << 0,  // word access at odd address during the instruction
    kPendDefer = 1u << 1,    // LDC/ANDC/ORC/XORC/RTE: the next instruction always executes
    kPendTrace = 1u << 2,    // SR.T = 1
    kPendNmi = 1u << 3,
    kPendIrq = 1u << 4,      // irq_level_ > interrupt mask
    kPendSleep = 1u << 5,    // SLEEP executed
    kPendBreak = 1u << 6,    // another bus master (DTC) wants the next instruction boundary
  };
  static constexpr s32 kForce = 1 << 28;
  static constexpr s32 kMaxSlice = 1 << 24;

  // Approximate states for exception sequences not covered by the instruction
  // tables (interrupt: table 5-4 incl. 2 states priority decision).
  static constexpr unsigned kIrqStatesMin = 18, kIrqStatesMax = 23;
  static constexpr unsigned kExcStatesMin = 16, kExcStatesMax = 21;
  static constexpr unsigned kSleepIdleStates = 2;

  Cpu(Bus& bus, const ChipConfig& cfg);
  ~Cpu() override;

  // Hardware reset: latch mode, SR.T=0, SR.I=7, load PC (and CP) from vector 0.
  void reset();

  // Execute one instruction (or take one pending exception / idle in sleep).
  // Returns states used.
  unsigned step();
  // Run until at least `states` have elapsed; returns states actually used
  // (may overshoot by one instruction).
  u64 run(u64 states);
  // At an instruction boundary (between run()/step() calls): take any pending
  // exception right away instead of after the next instruction.  Used by the
  // machine after scheduler events have run.  Returns states used.
  u64 poll();
  // At an instruction boundary: the bus was used by another master (DTC data
  // transfer) for `states`; the CPU waited.
  void stall(u64 states) { total_states_ += states; }

  // Interrupt controller interface.  `level` 1..7 (compared against I2-I0),
  // 0 = no request.  The request is level-sensitive: the controller must
  // deassert / re-evaluate it after the CPU accepts.
  void set_irq(u8 level, u8 vector) { irq_level_ = level; irq_vector_ = vector; reeval_irq(); }
  void request_nmi() { raise(kPendNmi); }
  // End the running slice after the current instruction so the Machine can
  // serve a DTC request there (the DTC runs between instructions, not at the
  // next scheduler event).
  void request_break() { raise(kPendBreak); }
  bool irq_accepted() const { return irq_taken_; }  // set for one step()/run() after acceptance
  void set_irq_ack_sink(IrqAckSink* s) { irq_ack_ = s; }
  // Called when TRAPA #n is executed, before the exception is taken; return
  // false to swallow the instruction (a host hijacking the CPU can keep a
  // kernel call from running in its context).
  void set_trapa_hook(std::function<bool(u8 vector)> hook) { trapa_hook_ = std::move(hook); }
  // An exception (address error, trace, NMI) is waiting for the next boundary.
  bool exception_pending() const { return (pending_ & (kPendAddrErr | kPendTrace | kPendNmi)) != 0; }
  void discard_pending_exceptions() { clear_pending(kPendAddrErr | kPendTrace | kPendDefer); }
  void set_sleeping(bool s) { sleeping_ = s; }

  Regs& regs() { return regs_; }
  const Regs& regs() const { return regs_; }
  Bus& bus() { return bus_; }
  bool max_mode() const { return max_mode_; }
  bool sleeping() const { return sleeping_; }
  // Elapsed states.  Live while a slice is running (e.g. read by a device
  // during an MMIO access): resolves to the start of the current instruction.
  u64 total_states() const {
    return in_slice_ ? total_states_ + u64(s64(slice_len_) - s64(true_budget())) : total_states_;
  }
  u64 now() const override { return total_states(); }
  u8 interrupt_mask() const { return u8((regs_.sr & kMaskBits) >> kMaskShift); }

  // Address helpers (page register selection, Table 1-10 notes).
  u32 code_addr(u16 a) const { return max_mode_ ? ((u32(regs_.cp) << 16) | a) : a; }
  u32 stack_addr(u16 a) const { return max_mode_ ? ((u32(regs_.tp) << 16) | a) : a; }
  u32 data_addr(unsigned reg, u16 a) const {
    if (!max_mode_) return a;
    const u8 page = reg < 4 ? regs_.dp : reg < 6 ? regs_.ep : regs_.tp;
    return (u32(page) << 16) | a;
  }
  u32 abs16_addr(u16 a) const { return max_mode_ ? ((u32(regs_.dp) << 16) | a) : a; }
  u32 abs8_addr(u8 a) const { return (u32(regs_.br) << 8) | a; }

  // Decode the instruction at code address `addr` without executing it.
  DecodedInsn decode_at(u32 addr) const;

  // Instruction cache maintenance.
  void invalidate_all();                     // drop every decoded cell
  void invalidate_range(u32 addr, u32 len);  // drop cells covering [addr, addr+len)
  void code_line_written(u32 addr) override;
  size_t cache_pages_allocated() const;

  // Statistics / debugging.
  u64 instructions_executed() const { return insn_count_; }
  u64 exceptions_taken() const { return exc_count_; }
  // Exceptions other than interrupts: address error, trace, invalid
  // instruction, zero divide, TRAPA, TRAP/VS.
  u64 faults_taken() const { return fault_count_; }

 private:
  friend struct ExecImpl;

  // --- operand memory access (records the line attribute for cycle accounting)
  u32 mem_read8(u32 a) {
    a &= bus_.addr_mask();
    const u8 at = bus_.attr(a);
    last_attr_ = at;
    return (at & Bus::kRdSlow) ? bus_.slow_read8(a) : bus_.mem()[a];
  }
  u32 mem_read16(u32 a) {
    a &= bus_.addr_mask();
    if (H8_UNLIKELY(a & 1)) { raise(kPendAddrErr); a &= ~1u; }
    const u8 at = bus_.attr(a);
    last_attr_ = at;
    return (at & Bus::kRdSlow) ? bus_.slow_read16(a) : Bus::be16(bus_.mem() + a);
  }
  void mem_write8(u32 a, u32 v) {
    a &= bus_.addr_mask();
    const u8 at = bus_.attr(a);
    last_attr_ = at;
    if (H8_UNLIKELY(at & Bus::kWrSlow)) bus_.slow_write8(a, u8(v)); else bus_.mem()[a] = u8(v);
  }
  void mem_write16(u32 a, u32 v) {
    a &= bus_.addr_mask();
    if (H8_UNLIKELY(a & 1)) { raise(kPendAddrErr); a &= ~1u; }
    const u8 at = bus_.attr(a);
    last_attr_ = at;
    if (H8_UNLIKELY(at & Bus::kWrSlow)) bus_.slow_write16(a, u16(v)); else Bus::put_be16(bus_.mem() + a, u16(v));
  }
  void push16(u16 v) {
    regs_.r[7] = u16(regs_.r[7] - 2);
    mem_write16(stack_addr(regs_.r[7]), v);
  }
  u16 pop16() {
    const u16 v = u16(mem_read16(stack_addr(regs_.r[7])));
    regs_.r[7] = u16(regs_.r[7] + 2);
    return v;
  }

  // --- instruction cache -----------------------------------------------------
  // Cells of page `cp` (page 0 in minimum mode), allocating on first use.
  // Also makes that page the current one, so pc = ip - page_cells_.
  const Cell* cells_for(u8 cp, u16 pc) {
    const unsigned page = max_mode_ ? cp : 0;
    Cell* cells = pages_[page].get();
    if (H8_UNLIKELY(!cells)) cells = alloc_page(page);
    page_cells_ = cells;
    return cells + pc;
  }
  Cell* alloc_page(unsigned page);
  void reset_cells(unsigned page, u32 first, u32 count);

  // --- pending / budget ------------------------------------------------------
  void raise(u32 bit) {
    if (!pending_) budget_ -= kForce;
    pending_ |= bit;
  }
  void clear_pending(u32 bit) {
    if (pending_ & bit) {
      pending_ &= ~bit;
      if (!pending_) budget_ += kForce;
    }
  }
  void reeval_irq() {
    if (irq_level_ && irq_level_ > interrupt_mask()) raise(kPendIrq); else clear_pending(kPendIrq);
  }
  void reeval_trace() {
    if (regs_.sr & kT) raise(kPendTrace); else clear_pending(kPendTrace);
  }
  void set_budget(s32 states) { budget_ = states - (pending_ ? kForce : 0); }
  s32 true_budget() const { return budget_ + (pending_ ? kForce : 0); }

 public:
  // End the running slice at absolute state `at` (at the first instruction
  // boundary at or after it).  The scheduler calls this when an event
  // scheduled during the slice is due before the slice would have ended, so
  // a peripheral event caused by a CPU write fires right after that write.
  void cut_slice(u64 at) {
    if (!in_slice_ || sleeping_) return;
    const s64 left = true_budget();
    const s64 now = s64(total_states_) + (s64(slice_len_) - left);
    s64 remaining = s64(at) - now;
    if (remaining < 0) remaining = 0;
    if (remaining >= left) return;
    const s32 delta = s32(left - remaining);
    slice_len_ -= delta;
    budget_ -= delta;
    cut_ = true;  // run() returns after this slice so the machine can fire the event
  }

 private:

  u64 run_slice(s32 states);
  void service_pending();
  void enter_exception(u8 vector, u16 push_pc, int new_mask);
  unsigned exception_states() const { return max_mode_ ? kExcStatesMax : kExcStatesMin; }
  unsigned irq_states() const { return max_mode_ ? kIrqStatesMax : kIrqStatesMin; }
  // Control register access at the size given by the instruction's Sz bit.
  u32 read_cr(u8 cr, Size sz) const;
  void write_cr(u8 cr, u32 v, Size sz);
  void set_sr(u16 v);

  Bus& bus_;
  ChipConfig cfg_;
  IrqAckSink* irq_ack_ = nullptr;
  std::function<bool(u8)> trapa_hook_;
  Regs regs_;
  bool max_mode_ = false;
  bool sleeping_ = false;
  bool irq_taken_ = false;
  u8 irq_level_ = 0;
  u8 irq_vector_ = 0;
  u8 last_attr_ = 0;   // bus attribute byte of the last operand access (class + wait states)

  s32 budget_ = 0;
  u32 pending_ = 0;
  s32 slice_len_ = 0;      // budget the current slice started with
  bool in_slice_ = false;
  bool cut_ = false;
  // Interrupt-mask update delay (H8/510 4.8.1 note): a mask written by
  // LDC/ANDC/ORC/XORC/RTE takes effect on the third state after that
  // instruction, so a 2-state successor (NOP) is not enough for an interrupt
  // to be accepted after it.
  bool mask_changed_ = false;      // SR.I2-I0 changed by a control-register write
  u64 mask_effective_at_ = 0;      // interrupts are accepted from this state on after a mask change
  u64 deferred_boundary_ = ~u64(0);  // boundary skipped by a deferral: no exception until an instruction ran
  Cell* page_cells_ = nullptr;   // cell array of the current code page
  std::unique_ptr<Cell[]> pages_[256];

  u64 total_states_ = 0;
  u64 insn_count_ = 0;
  u64 exc_count_ = 0;
  u64 fault_count_ = 0;
};

}  // namespace h8500

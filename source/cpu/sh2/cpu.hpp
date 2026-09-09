// SH-2 CPU core (SH7014/16/17).
//
// Same execution model as the H8/500 core: every instruction word owns a
// pre-decoded Cell; handlers are templates chained with tail calls; a signed
// state budget per slice is the only per-instruction check, and every
// boundary condition (address error, interrupt, deferral after an
// interrupt-disabling instruction, sleep) forces it negative once.
//
// SH-2 specifics:
//   * Delayed branches execute the following instruction (the delay slot)
//     before jumping.  The branch cell carries the slot's decoding: its
//     handler decides the target (prelude), chains to the slot handler, which
//     chains to the completion, so the pair is atomic and no interrupt can
//     land between them.  PC-relative targets inside the page are resolved to
//     cell pointers when the cell is filled.
//   * Timing: the pipeline executes most instructions in one state; data
//     accesses add the bus cycles beyond the one the MA stage overlaps, from
//     a per-class table.  Instruction fetch from external memory is charged
//     per 4-byte fetch line: as a constant baked into the line-start cell when
//     the area is not cached, or through the 1 KB direct-mapped instruction
//     cache model (section 7) when CCR enables the cache for it.  Retiming a
//     class or changing CCR drops every decoded cell.
//   * The PC visible to instructions is the instruction address + 4.
#pragma once
#include <array>
#include <memory>
#include <vector>

#include "common/sched.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/chip.hpp"
#include "cpu/sh2/decode.hpp"
#include "cpu/sh2/icache.hpp"

#if defined(__GNUC__) || defined(__clang__)
#define SH2_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define SH2_UNLIKELY(x) (x)
#endif

namespace sh2 {

// Notified when the CPU accepts an interrupt (so the controller can drop
// edge-latched requests and present the next one).
class IrqAckSink {
 public:
  virtual ~IrqAckSink() = default;
  virtual void irq_acknowledged(u8 vector) = 0;
};

class Cpu;
namespace detail {
const Cell* fill(Cpu& cpu, const Cell* c);
const Cell* page_end(Cpu& cpu, const Cell* c);
}  // namespace detail

class Cpu final : public CodeSink, public emu::Clock {
 public:
  struct Regs {
    u32 r[16] = {};
    u32 sr = 0xF0, gbr = 0, vbr = 0, mach = 0, macl = 0, pr = 0, pc = 0;
  };

  // SR bits.
  static constexpr u32 kT = 1u << 0, kS = 1u << 1, kQ = 1u << 8, kM = 1u << 9;
  static constexpr u32 kIShift = 4, kIMask = 0xF0;
  static constexpr u32 kSrMask = 0x3F3;

  // Pending-condition bits.
  static constexpr u32 kPendAddrErr = 1u << 0;     // CPU address error (vector 9)
  static constexpr u32 kPendDmaAddrErr = 1u << 1;  // DMAC address error (vector 10)
  static constexpr u32 kPendDefer = 1u << 2;       // instruction after LDC/STC/LDS/STS: no interrupt
  static constexpr u32 kPendNmi = 1u << 3;
  static constexpr u32 kPendIrq = 1u << 4;
  static constexpr u32 kPendSleep = 1u << 5;

  // Exception vectors (table 5.3).
  static constexpr u8 kVecIllegal = 4, kVecSlotIllegal = 6, kVecCpuAddrErr = 9, kVecDmaAddrErr = 10, kVecNmi = 11;

  static constexpr s32 kForce = 1 << 28;
  static constexpr s32 kMaxSlice = 1 << 24;
  // Exception sequences (table 6.5): 5 + m1 + m2 + m3 from start of processing;
  // interrupt response adds the priority decision (2 states, 3 for IRQ pins;
  // the m terms come out of the stack / vector accesses themselves).
  static constexpr unsigned kExcStates = 5, kIrqStates = 7, kIrqPinExtra = 2;

  Cpu(Bus& bus, const ChipConfig& cfg);
  ~Cpu() override;

  // Power-on reset: PC and SP from vectors 0/1, VBR = 0, SR.I = 15.
  void reset();

  unsigned step();
  u64 run(u64 states);
  u64 poll();
  void stall(u64 states) { total_states_ += states; }

  // Interrupt controller interface.  level 1..15 (compared against I3-I0),
  // 0 = no request; `pin` marks an external IRQ (longer response time).
  void set_irq(u8 level, u8 vector, bool pin = false) {
    irq_level_ = level;
    irq_vector_ = vector;
    irq_pin_ = pin;
    reeval_irq();
  }
  void request_nmi() { raise(kPendNmi); }
  void request_dma_address_error() { raise(kPendDmaAddrErr); }
  bool irq_accepted() const { return irq_taken_; }
  void set_irq_ack_sink(IrqAckSink* s) { irq_ack_ = s; }

  Regs& regs() { return regs_; }
  const Regs& regs() const { return regs_; }
  Bus& bus() { return bus_; }
  bool sleeping() const { return sleeping_; }
  // The interrupt the INTC currently presents (level 0 = none) and its vector.
  u8 irq_level() const { return irq_level_; }
  u8 irq_vector() const { return irq_vector_; }
  bool in_standby() const { return standby_; }
  // SBYCR.SBY: a SLEEP instruction enters standby (only NMI / reset wake) instead of sleep.
  void set_standby_request(bool sby) { standby_request_ = sby; }
  // CCR: which areas the instruction cache covers (bits 0-3 CS0-3, bit 4 DRAM).
  void set_cache_control(u8 ccr);
  u8 cache_control() const { return ccr_; }
  void invalidate_cache_tags() { cache_tags_.fill(0); }

  u64 total_states() const {
    return in_slice_ ? total_states_ + u64(s64(slice_len_) - s64(true_budget())) : total_states_;
  }
  u64 now() const override { return total_states(); }
  u8 interrupt_mask() const { return u8((regs_.sr & kIMask) >> kIShift); }

  DecodedInsn decode_at(u32 addr) const { return decode(const_cast<Bus&>(bus_).read16(addr)); }

  void invalidate_all();
  void invalidate_range(u32 addr, u32 len);
  void code_line_written(u32 addr) override;
  void code_timing_changed() override { invalidate_all(); }
  size_t cache_pages_allocated() const;

  u64 instructions_executed() const { return insn_count_; }
  u64 exceptions_taken() const { return exc_count_; }

 private:
  friend struct ExecImpl;
  friend const Cell* detail::fill(Cpu&, const Cell*);
  friend const Cell* detail::page_end(Cpu&, const Cell*);

  // --- data access: alignment check, access penalty, slow path ---------------
  u32 read8(u32 a) {
    a &= addr_mask_;
    u32 off;
    if (SH2_UNLIKELY(!bus_.translate(a, off))) return 0xFF;
    const u8 at = bus_.attr_off(off);
    budget_ -= s32(bus_.penalty(at, 0));
    return (at & Bus::kRdSlow) ? bus_.slow_read8(a, off) : bus_.mem()[off];
  }
  u32 read16(u32 a) {
    a &= addr_mask_;
    if (SH2_UNLIKELY(a & 1)) { raise(kPendAddrErr); a &= ~1u; }
    u32 off;
    if (SH2_UNLIKELY(!bus_.translate(a, off))) return 0xFFFF;
    const u8 at = bus_.attr_off(off);
    budget_ -= s32(bus_.penalty(at, 1));
    return (at & Bus::kRdSlow) ? bus_.slow_read16(a, off) : Bus::be16(bus_.mem() + off);
  }
  u32 read32(u32 a) {
    a &= addr_mask_;
    if (SH2_UNLIKELY(a & 3)) { raise(kPendAddrErr); a &= ~3u; }
    u32 off;
    if (SH2_UNLIKELY(!bus_.translate(a, off))) return 0xFFFFFFFFu;
    const u8 at = bus_.attr_off(off);
    budget_ -= s32(bus_.penalty(at, 2));
    return (at & Bus::kRdSlow) ? bus_.slow_read32(a, off) : Bus::be32(bus_.mem() + off);
  }
  void write8(u32 a, u32 v) {
    a &= addr_mask_;
    u32 off;
    if (SH2_UNLIKELY(!bus_.translate(a, off))) return;
    const u8 at = bus_.attr_off(off);
    budget_ -= s32(bus_.penalty(at, 0));
    if (SH2_UNLIKELY(at & Bus::kWrSlow)) bus_.slow_write8(a, off, u8(v)); else bus_.mem()[off] = u8(v);
  }
  void write16(u32 a, u32 v) {
    a &= addr_mask_;
    if (SH2_UNLIKELY(a & 1)) { raise(kPendAddrErr); a &= ~1u; }
    u32 off;
    if (SH2_UNLIKELY(!bus_.translate(a, off))) return;
    const u8 at = bus_.attr_off(off);
    budget_ -= s32(bus_.penalty(at, 1));
    if (SH2_UNLIKELY(at & Bus::kWrSlow)) bus_.slow_write16(a, off, u16(v)); else Bus::put_be16(bus_.mem() + off, u16(v));
  }
  void write32(u32 a, u32 v) {
    a &= addr_mask_;
    if (SH2_UNLIKELY(a & 3)) { raise(kPendAddrErr); a &= ~3u; }
    u32 off;
    if (SH2_UNLIKELY(!bus_.translate(a, off))) return;
    const u8 at = bus_.attr_off(off);
    budget_ -= s32(bus_.penalty(at, 2));
    if (SH2_UNLIKELY(at & Bus::kWrSlow)) bus_.slow_write32(a, off, v); else Bus::put_be32(bus_.mem() + off, v);
  }
  void push32(u32 v) { regs_.r[15] -= 4; write32(regs_.r[15], v); }
  u32 pop32() { const u32 v = read32(regs_.r[15]); regs_.r[15] += 4; return v; }

  // --- instruction cells -----------------------------------------------------
  // Cell of the instruction at `pc`, allocating its page on first use and
  // making that page current (pc_of() works for cells of the current page).
  const Cell* cells_for(u32 pc) {
    const u32 page = pc >> kPageShift;
    Cell* cells = pages_[page].get();
    if (SH2_UNLIKELY(!cells)) cells = alloc_page(page);
    page_cells_ = cells;
    page_pc_ = page << kPageShift;
    return cells + ((pc & (kPageSize - 1)) >> 1);
  }
  u32 pc_of(const Cell* c) const { return page_pc_ + u32(c - page_cells_) * 2; }
  // Branch target: odd addresses raise an address error; a target in the
  // second word of an external fetch line pays for that line.
  const Cell* branch_to(u32 pc) {
    if (SH2_UNLIKELY(pc & 1)) {
      raise(kPendAddrErr);
      pc &= ~1u;
    }
    const Cell* cell = SH2_UNLIKELY((pc ^ page_pc_) >> kPageShift) ? cells_for(pc)
                                                                   : page_cells_ + ((pc & (kPageSize - 1)) >> 1);
    if (pc & 2) fetch_line_at(pc);
    return cell;
  }
  Cell* alloc_page(u32 page);
  void reset_cells(u32 page, u32 first, u32 count);
  void fill_cell(Cell* c, u32 pc);
  bool cache_covers(u32 pc) const {
    const unsigned area = pc < 0x01000000u ? ((pc >> 22) & 3) : 4;
    return (ccr_ >> area) & 1;
  }
  // Instruction cache model for the 4-byte line at `pc` (cache enabled for
  // the area): a hit costs nothing, a miss the fill plus one idle state (no
  // idle for consecutive misses), and the first hit after a miss is treated
  // as a miss (7.4.3, 7.4.4).
  void fetch_line_cached(u32 pc, u8 cls) {
    const unsigned entry = (pc >> 2) & 0xFF;
    const u16 tag = u16(((pc >> 10) & 0x7FFF) | 0x8000);
    const u32 fill = bus_.fetch_fill(cls);
    if (cache_tags_[entry] == tag) {
      if (cache_miss_last_) {
        cache_miss_last_ = false;
        budget_ -= s32(fill);
      }
      return;
    }
    cache_tags_[entry] = tag;
    budget_ -= s32(fill + (cache_miss_last_ ? 0 : 1));
    cache_miss_last_ = true;
  }
  // Fetch accounting for a branch landing on the second word of a line.
  void fetch_line_at(u32 pc) {
    const u8 at = bus_.attr(pc);
    if (!bus_.external(at)) return;
    const u8 cls = u8(at & Bus::kClassMask);
    pc &= ~3u;
    if (bus_.cacheable(at) && cache_covers(pc)) fetch_line_cached(pc, cls);
    else budget_ -= s32(bus_.fetch_static(cls));
  }

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
  void set_budget(s32 states) { budget_ = states - (pending_ ? kForce : 0); }
  s32 true_budget() const { return budget_ + (pending_ ? kForce : 0); }

 public:
  // End the running slice at absolute state `at` (at the first instruction
  // boundary at or after it).  The scheduler calls this when an event
  // scheduled during the slice is due before the slice would have ended.
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
  u32 wake_mask() const { return standby_ ? kPendNmi : (kPendNmi | kPendIrq | kPendDmaAddrErr); }

  u64 run_slice(s32 states);
  void service_pending();
  // Push SR and PC, set the mask (if >= 0), jump through the vector table.
  void enter_exception(u8 vector, u32 push_pc, int new_mask);
  void set_sr(u32 v) { regs_.sr = v & kSrMask; reeval_irq(); }

  Bus& bus_;
  ChipConfig cfg_;
  u32 addr_mask_ = 0xFFFFFFFFu;  // data addresses are masked to the decoded bits (SH7034: A0-A26)
  IrqAckSink* irq_ack_ = nullptr;
  Regs regs_;
  bool sleeping_ = false, standby_ = false, standby_request_ = false;
  bool irq_taken_ = false, irq_pin_ = false;
  u8 irq_level_ = 0, irq_vector_ = 0;
  u32 pending_ = 0;
  s32 budget_ = 0;
  s32 slice_len_ = 0;
  bool in_slice_ = false;
  bool cut_ = false;
  u64 total_states_ = 0, insn_count_ = 0, exc_count_ = 0;

  // delayed branch in flight: decided by the prelude, consumed by the completion
  const Cell* branch_cell_ = nullptr;
  Handler branch_finish_ = nullptr;
  u32 branch_target_ = 0, branch_sr_ = 0;
  u8 branch_fetch_ = 0;

  // instruction cache model (section 7)
  u8 ccr_ = 0;
  bool cache_miss_last_ = false;
  std::array<u16, 256> cache_tags_{};

  // decoded cells: one page of 32768 cells (+1 guard) per 64 KB of code
  std::vector<std::unique_ptr<Cell[]>> pages_;
  Cell* page_cells_ = nullptr;
  u32 page_pc_ = 0;
};

}  // namespace sh2

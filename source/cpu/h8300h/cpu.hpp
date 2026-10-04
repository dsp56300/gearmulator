// H8/300H CPU core: registers, CCR, exception entry and the execution loop.
//
// Same execution model as the other cached threaded-code cores: every guest
// code word owns a pre-decoded Cell, handlers are chained with tail calls, a
// signed state budget per slice is the only per-instruction check and the
// boundary conditions force it negative once.
//
// Timing follows the H8/300H manual's access-based model: every instruction
// pays for the words it fetches (static, in the cell) and every data access
// pays by the bus area it hits (dynamic, through the bus access classes),
// plus the internal-operation states of the few instructions that have them.
#pragma once
#include <functional>

#include "common/core.hpp"
#include "cpu/h8300h/bus.hpp"
#include "cpu/h8300h/decode.hpp"
#include "cpu/h8300h/icache.hpp"

namespace h8300h {

using Scheduler = emu::Scheduler;
using Clock = emu::Clock;

// The interrupt controller and system registers the core consults,
// implemented by the peripheral block.
class IntcBlock {
 public:
  virtual ~IntcBlock() = default;
  // The request to take at the next boundary, given the CCR: NMI first, then
  // the highest of the acceptable maskable ones.
  virtual bool arbitrate(unsigned& which, u8 ccr) const = 0;
  virtual void acknowledge(unsigned which) = 0;
  // A request that ends software standby (NMI, IRQ0-IRQ2).
  virtual bool standby_wake(u8 ccr) const = 0;
  virtual u8 syscr() const = 0;
  virtual void enter_software_standby() = 0;
};

class Cpu final : public emu::SliceCore {
 public:
  static constexpr u32 kPendIrq = 1u << 0;
  static constexpr u32 kPendSleep = 1u << 1;     // SLEEP executed
  static constexpr u32 kPendRefetch = 1u << 2;
  static constexpr u32 kPendHoldoff = 1u << 3;   // the held-off instruction runs alone (see run_slice)
  static constexpr u32 kPendFault = 1u << 4;     // undefined instruction: the core stops advancing
  static constexpr u32 kPendReset = 1u << 5;     // a reset is due at the next boundary (watchdog)

  Cpu(Bus& bus);
  ~Cpu() override;

  // Hardware reset: PC from vector 0, CCR = I set, registers cleared.
  void reset();

  unsigned step();
  u64 run(u64 states);
  u64 poll();

  void set_intc(IntcBlock* b) { intc_ = b; reeval_irq(); }
  void reeval_irq();
  bool irq_accepted() const { return irq_taken_; }
  // Stop at the next boundary and idle until reset() (the watchdog reset).
  void request_reset() { raise(kPendReset); }
  bool reset_requested() const { return (pending_ & kPendReset) != 0; }

  // --- registers -------------------------------------------------------------
  u32 pc() const { return pc_; }
  void set_pc(u32 v) { pc_ = v & kAddrMask; }
  // Address of the instruction being executed (pc() is only valid between run() / step() calls).
  u32 current_pc() const { return in_slice_ && cur_ip_ ? pc_of(cur_ip_) : pc_; }
  u8 ccr() const { return ccr_; }
  void set_ccr(u8 v) { ccr_ = v; reeval_irq(); }
  u32 er(unsigned n) const { return er_[n & 7]; }
  void set_er(unsigned n, u32 v) { er_[n & 7] = v; }
  // Byte registers: 0-7 = R0H-R7H, 8-15 = R0L-R7L.  Word: 0-7 = R0-R7, 8-15 = E0-E7.
  u8 reg8(unsigned n) const { const u32 v = er_[n & 7]; return (n & 8) ? u8(v) : u8(v >> 8); }
  void set_reg8(unsigned n, u8 b) {
    u32& v = er_[n & 7];
    v = (n & 8) ? ((v & ~0xFFu) | b) : ((v & ~0xFF00u) | (u32(b) << 8));
  }
  u16 reg16(unsigned n) const { const u32 v = er_[n & 7]; return (n & 8) ? u16(v >> 16) : u16(v); }
  void set_reg16(unsigned n, u16 w) {
    u32& v = er_[n & 7];
    v = (n & 8) ? ((v & 0x0000FFFFu) | (u32(w) << 16)) : ((v & 0xFFFF0000u) | w);
  }
  u32 sp() const { return er_[7]; }
  void set_sp(u32 v) { er_[7] = v; }
  bool sleeping() const { return sleeping_; }
  bool software_standby() const { return standby_; }
  // An undefined instruction was met: the core is stuck on it, like the
  // hardware would be on a real fault, until reset.
  bool faulted() const { return faulted_; }
  u32 fault_address() const { return fault_addr_; }

  // --- instruction cache -------------------------------------------------------
  void invalidate_all();
  void invalidate_range(u32 addr, u32 len);
  void select_code_bank(u32 base, u32 size, u32 key);
  void code_written(u32 addr) {
    if (cells_.page_if_allocated(addr & kAddrMask)) invalidate_range(addr, 1);
  }
  void code_line_written(u32 addr) override;
  // Bus timing changed (bus controller registers): the static fetch costs in
  // every cell are stale.  The instruction that changed them was fetched at
  // the old timing and finishes on its cell; the cache is dropped at the
  // next slice start.
  void code_timing_changed() override { retime_ = true; raise(kPendRefetch); }
  // Cells bake in the fetch cost only (data accesses are costed as they happen), so a retimed
  // class only stales code fetched through it. Firmware that toggles the area 2 (ESP) access
  // states around every ESP access would otherwise drop all cells each time, which costs more
  // than the emulation itself.
  void code_class_timing_changed(unsigned cls) override { retime_classes_ |= u16(1u << cls); raise(kPendRefetch); }

  DecodedInsn decode_at(u32 addr) const;
  // States one data access of `bytes` at `a` costs (area, RAM or register field).
  u32 access_states(u32 a, unsigned bytes) const;

 private:
  friend struct ExecImpl;
  friend class Peripherals;

  // --- memory (data accesses, charged) ---------------------------------------------
  void charge(u32 a, unsigned bytes) { acc_ += access_states(a, bytes); }
  u8 rd8(u32 a) { a &= kAddrMask; charge(a, 1); return bus_.read8(a); }
  u16 rd16(u32 a) { a &= kAddrMask; charge(a, 2); return bus_.read16(a); }
  u32 rd32(u32 a) { a &= kAddrMask; charge(a, 2); charge((a + 2) & kAddrMask, 2); return bus_.read32(a); }
  void wr8(u32 a, u8 v) { a &= kAddrMask; charge(a, 1); bus_.write8(a, v); }
  void wr16(u32 a, u16 v) { a &= kAddrMask; charge(a, 2); bus_.write16(a, v); }
  void wr32(u32 a, u32 v) { a &= kAddrMask; charge(a, 2); charge((a + 2) & kAddrMask, 2); bus_.write32(a, v); }
  void push32(u32 v) { const u32 s = er_[7] - 4; wr32(s, v); er_[7] = s; }
  u32 pop32() { const u32 s = er_[7]; er_[7] = s + 4; return rd32(s); }
  void push16(u16 v) { const u32 s = er_[7] - 2; wr16(s, v); er_[7] = s; }
  u16 pop16() { const u32 s = er_[7]; er_[7] = s + 2; return rd16(s); }
  // Static fetch cost of the `words` words of the instruction at `pc`.
  u32 fetch_states(u32 pc, unsigned words) const;

  // --- cells -----------------------------------------------------------------------
  const Cell* cell_at(u32 pc) {
    pc &= kAddrMask & ~1u;
    if (EMU_UNLIKELY((pc ^ page_pc_) >> CellPages::kPageShift)) return cells_for(pc);
    return page_cells_ + ((pc & (CellPages::kPageSize - 1)) >> 1);
  }
  const Cell* cells_for(u32 pc) {
    pc &= kAddrMask & ~1u;
    page_cells_ = cells_.page(pc);
    page_pc_ = CellPages::page_base(pc);
    return page_cells_ + CellPages::index_in_page(pc);
  }
  u32 pc_of(const Cell* ip) const { return (page_pc_ + u32(ip - page_cells_) * 2) & kAddrMask; }

  // --- exceptions ---------------------------------------------------------------------
  // Vectored entry: push PC with the CCR, mask, load the vector.  Returns
  // the states used (the stack and vector accesses plus the internal ones).
  u32 enter_exception(unsigned vector);
  bool take_interrupt();
  void hold_off() { holdoff_ = true; raise(kPendHoldoff); }

  u64 run_slice(s32 states);

  Bus& bus_;
  IntcBlock* intc_ = nullptr;

  u32 er_[8] = {};
  u32 pc_ = 0;
  u8 ccr_ = 0x80;
  u32 acc_ = 0;           // dynamic states of the instruction in flight
  bool holdoff_ = false;  // the next boundary executes an instruction regardless of requests
  bool sleeping_ = false;
  bool standby_ = false;
  bool faulted_ = false;
  u32 fault_addr_ = 0;
  bool irq_taken_ = false;
  bool retime_ = false;   // drop every cell at the next slice start
  u16 retime_classes_ = 0;  // drop the cells fetched through these classes at the next slice start

  static constexpr u32 kNoPage = 0xFFFFFFFFu;
  CellPages cells_;
  Cell* page_cells_ = nullptr;
  u32 page_pc_ = kNoPage;
  const Cell* cur_ip_ = nullptr;
};

}  // namespace h8300h

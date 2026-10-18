// SH-3 CPU core (SH7709A).
//
// Same execution model as the SH-2 core: every instruction word owns a
// pre-decoded Cell; handlers are templates chained with tail calls; a
// signed state budget per slice is the only per-instruction check, and every
// boundary condition (interrupt, sleep) forces it negative once.  Delayed
// branches carry their slot in the branch cell so the pair is atomic.
//
// The instruction set, the cell layout and the effects of the ordinary
// instructions are the SH family's (cpu/sh/); this core adds the SH-3's
// exception model, its register banks and its timing.
//
// SH-3 specifics:
//   * Two banks of R0-R7 selected by SR.RB; the inactive bank lives in
//     bank_[].  Exceptions switch to bank 1, RTE restores.
//   * Exceptions save SR / PC into SSR / SPC and jump to VBR + H'100
//     (general) or VBR + H'600 (interrupts) with the event code in EXPEVT
//     or INTEVT (and the SH7709A's INTEVT2); SR.BL blocks interrupts until
//     the handler clears it.  Interrupts are accepted when their priority
//     exceeds SR.IMASK.
//   * Big-endian data, as the boards this core serves strap the chip.
//   * Timing: one issue cycle per instruction (a few take more, per the
//     manual's issue rates), plus the penalty of the access class of every
//     data access (zero for cached memory and on-chip resources).  The
//     cache itself is not modelled.
//   * The MMU is not modelled (MMUCR.AT is expected to stay clear); P0-P3
//     alias the physical space through the bus page table.
//   * A misaligned data access is masked to the alignment (the hardware
//     raises an address error) and counted: under emulation it is always a
//     bug somewhere.
#pragma once
#include "common/core.hpp"
#include "cpu/sh/insn.hpp"
#include "cpu/sh3/bus.hpp"
#include "cpu/sh3/decode.hpp"
#include "cpu/sh3/icache.hpp"

namespace sh3 {

// The interrupt controller the core consults: the highest-priority request
// (priority 1-15 for maskable sources, kNmiPriority for the NMI) with its
// INTEVT and INTEVT2 codes; acknowledge() drops edge-latched requests once
// accepted.
class IntcBlock {
 public:
  static constexpr int kNmiPriority = 16;
  virtual ~IntcBlock() = default;
  virtual int pending_priority(u32& intevt, u32& intevt2, unsigned& source) const = 0;
  virtual void acknowledge(unsigned source) = 0;
  // ICR1.BLMSK: the NMI is accepted even while SR.BL is set.
  virtual bool nmi_ignores_bl() const = 0;
};

class Cpu;
namespace detail {
const Cell* fill(Cpu& cpu, const Cell* c);
const Cell* page_end(Cpu& cpu, const Cell* c);
}  // namespace detail

class Cpu final : public emu::SliceCore {
 public:
  struct Regs {
    u32 r[16] = {};
    u32 sr = kSrMD | kSrRB | kSrBL | kSrIMask, gbr = 0, vbr = 0, ssr = 0, spc = 0;
    u32 mach = 0, macl = 0, pr = 0, pc = 0xA0000000u;
  };

  // MAC.L saturates the 48-bit accumulator in two's complement, as measured
  // on SH-4 silicon and described for the SH-3.
  static constexpr sh::MacLSaturation kMacLSaturation = sh::MacLSaturation::SignExtended48;

  // Pending-condition bits.
  static constexpr u32 kPendIrq = 1u << 0;
  static constexpr u32 kPendSleep = 1u << 1;
  static constexpr u32 kPendRefetch = 1u << 2;  // the current page's cells are stale

  // Exception handling overhead in states.
  static constexpr unsigned kExcStates = 5;

  explicit Cpu(Bus& bus);
  ~Cpu() override;

  // Power-on reset: PC = A0000000, SR = MD RB BL IMASK, VBR = 0, EXPEVT = 000.
  void reset();
  // Manual reset (the RESET pin while running): same entry with EXPEVT = 020.
  void manual_reset();

  unsigned step();
  u64 run(u64 states);
  u64 poll();
  // Asleep there is no instruction boundary to end the slice at.
  void cut_slice(u64 at) { if (!sleeping_) SliceCore::cut_slice(at); }

  void set_intc(IntcBlock* i) { intc_ = i; reeval_irq(); }
  void reeval_irq();
  bool irq_accepted() const { return irq_taken_; }

  Regs& regs() { return regs_; }
  const Regs& regs() const { return regs_; }
  // The inactive R0-R7 bank (bank 1 while SR.RB = 0, bank 0 while SR.RB = 1).
  u32 bank(unsigned i) const { return bank_[i & 7]; }
  void set_bank(unsigned i, u32 v) { bank_[i & 7] = v; }
  Bus& bus() { return bus_; }
  bool sleeping() const { return sleeping_; }
  u32 pc() const { return regs_.pc; }
  // Address of the instruction being executed, for a device hook called from
  // its memory access (pc() is only valid between run() / step() calls).
  u32 current_pc() const { return in_slice_ && cur_ip_ ? pc_of(cur_ip_) : regs_.pc; }
  void set_sr(u32 v);
  u8 interrupt_mask() const { return u8((regs_.sr & kSrIMask) >> kSrIShift); }

  // CCN registers the core owns.
  u32 expevt() const { return expevt_; }
  u32 intevt() const { return intevt_; }
  u32 intevt2() const { return intevt2_; }
  u32 tra() const { return tra_; }
  void set_expevt(u32 v) { expevt_ = v & 0xFFF; }
  void set_intevt(u32 v) { intevt_ = v & 0xFFF; }
  void set_intevt2(u32 v) { intevt2_ = v & 0xFFF; }
  void set_tra(u32 v) { tra_ = v & 0x3FC; }

  DecodedInsn decode_at(u32 addr) const { return decode(const_cast<Bus&>(bus_).read16(addr)); }

  void invalidate_all();
  void invalidate_range(u32 addr, u32 len);
  void code_line_written(u32 addr) override;
  void code_timing_changed() override { retime_ = true; raise(kPendRefetch); }
  size_t cache_pages_allocated() const;

  u64 instructions_executed() const { return insn_count_; }
  u64 exceptions_taken() const { return exc_count_; }
  u64 misaligned_accesses() const { return misaligned_count_; }

 private:
  friend struct ExecImpl;
  friend struct sh::Insn<Cpu>;
  friend const Cell* detail::fill(Cpu&, const Cell*);
  friend const Cell* detail::page_end(Cpu&, const Cell*);

  // --- data access ------------------------------------------------------------
  u32 read8(u32 a) {
    u32 off;
    if (EMU_UNLIKELY(!bus_.translate(a, off))) return bus_.read8(a);
    const u8 at = bus_.attr_off(off);
    budget_ -= s32(bus_.penalty(at, 0));
    return (at & Bus::kRdSlow) ? bus_.slow_read8(a, off) : bus_.mem()[off];
  }
  u32 read16(u32 a) {
    if (EMU_UNLIKELY(a & 1)) a = misaligned(a, 1, false);
    u32 off;
    if (EMU_UNLIKELY(!bus_.translate(a, off))) return bus_.read16(a);
    const u8 at = bus_.attr_off(off);
    budget_ -= s32(bus_.penalty(at, 1));
    return (at & Bus::kRdSlow) ? bus_.slow_read16(a, off) : Bus::get16(bus_.mem() + off);
  }
  u32 read32(u32 a) {
    if (EMU_UNLIKELY(a & 3)) a = misaligned(a, 3, false);
    u32 off;
    if (EMU_UNLIKELY(!bus_.translate(a, off))) return bus_.read32(a);
    const u8 at = bus_.attr_off(off);
    budget_ -= s32(bus_.penalty(at, 2));
    return (at & Bus::kRdSlow) ? bus_.slow_read32(a, off) : Bus::get32(bus_.mem() + off);
  }
  void write8(u32 a, u32 v) {
    u32 off;
    if (EMU_UNLIKELY(!bus_.translate(a, off))) { bus_.write8(a, u8(v)); return; }
    const u8 at = bus_.attr_off(off);
    budget_ -= s32(bus_.penalty(at, 0));
    if (EMU_UNLIKELY(at & Bus::kWrSlow)) bus_.slow_write8(a, off, u8(v)); else bus_.mem()[off] = u8(v);
  }
  void write16(u32 a, u32 v) {
    if (EMU_UNLIKELY(a & 1)) a = misaligned(a, 1, true);
    u32 off;
    if (EMU_UNLIKELY(!bus_.translate(a, off))) { bus_.write16(a, u16(v)); return; }
    const u8 at = bus_.attr_off(off);
    budget_ -= s32(bus_.penalty(at, 1));
    if (EMU_UNLIKELY(at & Bus::kWrSlow)) bus_.slow_write16(a, off, u16(v)); else Bus::put16(bus_.mem() + off, u16(v));
  }
  void write32(u32 a, u32 v) {
    if (EMU_UNLIKELY(a & 3)) a = misaligned(a, 3, true);
    u32 off;
    if (EMU_UNLIKELY(!bus_.translate(a, off))) { bus_.write32(a, v); return; }
    const u8 at = bus_.attr_off(off);
    budget_ -= s32(bus_.penalty(at, 2));
    if (EMU_UNLIKELY(at & Bus::kWrSlow)) bus_.slow_write32(a, off, v); else Bus::put32(bus_.mem() + off, v);
  }
  // Count a misaligned access and return the aligned address.
  u32 misaligned(u32 a, u32 mask, bool write);

  // --- instruction cells -----------------------------------------------------
  const Cell* cells_for(u32 pc) {
    page_cells_ = cells_.page(pc);
    page_pc_ = CellPages::page_base(pc);
    return page_cells_ + CellPages::index_in_page(pc);
  }
  u32 pc_of(const Cell* c) const { return page_pc_ + u32(c - page_cells_) * 2; }
  const Cell* branch_to(u32 pc) {
    pc &= ~1u;
    return EMU_UNLIKELY((pc ^ page_pc_) >> kPageShift) ? cells_for(pc) : page_cells_ + ((pc & (kPageSize - 1)) >> 1);
  }
  void fill_cell(Cell* c, u32 pc);

  u64 run_slice(s32 states);
  void service_pending();
  bool take_interrupt();
  // Exception entry: SSR/SPC, MD RB BL, EXPEVT or INTEVT, PC = VBR + offset.
  void enter_exception(u32 code, u32 save_pc, u32 vector_offset, bool interrupt);
  void switch_bank();

  Bus& bus_;
  IntcBlock* intc_ = nullptr;
  Regs regs_;
  u32 bank_[8] = {};
  u32 expevt_ = 0, intevt_ = 0, intevt2_ = 0, tra_ = 0;
  bool sleeping_ = false;
  bool irq_taken_ = false;
  bool retime_ = false;
  u64 insn_count_ = 0, exc_count_ = 0, misaligned_count_ = 0;
  const Cell* cur_ip_ = nullptr;  // the memory-accessing instruction in flight

  // delayed branch in flight: decided by the prelude, consumed by the completion
  const Cell* branch_cell_ = nullptr;
  Handler branch_finish_ = nullptr;
  u32 branch_target_ = 0;

  // decoded cells: one page of 32768 cells (+1 guard) per 64 KB of code
  CellPages cells_;
  Cell* page_cells_ = nullptr;
  u32 page_pc_ = 0;
};

}  // namespace sh3

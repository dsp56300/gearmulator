// MC6801 / HD6301 / MC68HC11 CPU core: registers, interrupt entry, WAI /
// SLP / STOP and the execution loop.
//
// Same execution model as the other cached threaded-code cores: every guest
// code byte owns a pre-decoded Cell, handlers are chained with tail calls, a
// signed state budget per slice is the only per-instruction check and the
// boundary conditions force it negative once.
//
// Time is counted in E cycles, one state each. Every instruction costs its
// fixed cycle count; a data access to a device line is made at the cycle of
// its bus cycle within the instruction (Cell::d0 / d1), so a peripheral that
// reads the time during the access (the free-running counter, a flag-clear
// sequence) sees the cycle the hardware would.
#pragma once
#include "common/core.hpp"
#include "cpu/m6801/bus.hpp"
#include "cpu/m6801/decode.hpp"
#include "cpu/m6801/icache.hpp"

namespace m6801 {

// The interrupt logic the core consults, implemented by the peripheral block.
// The MC6801 keeps one flip-flop per maskable source (IRQ1, IRQ2) that a
// request sets while the I bit is clear and that the I bit holds reset; the
// vector is chosen during the sequence from the requests present then
// (MC6801 reference manual 5.3). On the HC11 the non-maskable request is
// XIRQ, a level that the X bit masks, and every other source is a level too.
class IntcBlock {
 public:
  virtual ~IntcBlock() = default;
  // A latched NMI edge (HC11: XIRQ asserted while X is clear).
  virtual bool nmi_pending() const = 0;
  // A maskable request: a flip-flop set, or an IRQ1 / IRQ2 level active.
  virtual bool maskable_pending() const = 0;
  // Any request, masked or not: what ends SLP (HC11 STOP: XIRQ, or IRQ while I is clear).
  virtual bool sleep_release() const = 0;
  // The vector of the interrupt sequence being entered: NMI, IRQ1, ICF, OCF,
  // TOF, else SCI (also when an IRQ2 request disappeared meanwhile).
  virtual u16 acknowledge() = 0;
  // The I bit was set: the maskable flip-flops are cleared.
  virtual void mask_set() = 0;
};

class Cpu final : public emu::SliceCore {
 public:
  static constexpr u32 kPendIrq = 1u << 0;
  static constexpr u32 kPendSleep = 1u << 1;    // WAI / SLP / STOP executed
  static constexpr u32 kPendRefetch = 1u << 2;  // a code bank switched under the running slice

  Cpu(Bus& bus, ChipModel model);
  ~Cpu() override;

  // Reset: PC from $FFFE, I set (and S, X on the HC11). The other registers keep their values.
  void reset();
  // The operating mode decides which addresses fault on the HD6301 (an
  // instruction fetched from the register field traps).
  void set_mode(int mode);

  // One instruction (or the interrupt entry and the instruction it leads to,
  // or one idle state while waiting / sleeping). Returns the states used.
  unsigned step();
  // Run until at least `states` have elapsed (may overshoot by one instruction).
  u64 run(u64 states);
  // At an instruction boundary: take a pending interrupt, leave WAI / SLP.
  u64 poll();

  void set_intc(IntcBlock* b) { intc_ = b; reeval_irq(); }
  void reeval_irq();
  bool irq_accepted() const { return irq_taken_; }

  // --- registers ---------------------------------------------------------------
  u8 a() const { return a_; }
  u8 b() const { return b_; }
  u16 d() const { return u16((a_ << 8) | b_); }
  u16 x() const { return x_; }
  u16 y() const { return y_; }
  u16 sp() const { return s_; }
  u16 pc() const { return pc_; }
  u8 ccr() const { return cc_; }
  void set_a(u8 v) { a_ = v; }
  void set_b(u8 v) { b_ = v; }
  void set_d(u16 v) { a_ = u8(v >> 8); b_ = u8(v); }
  void set_x(u16 v) { x_ = v; }
  void set_y(u16 v) { y_ = v; }
  void set_sp(u16 v) { s_ = v; }
  void set_pc(u16 v) { pc_ = v; }
  void set_ccr(u8 v);
  // Address of the instruction being executed (pc() is only valid between run() / step() calls).
  u16 current_pc() const { return in_slice_ && cur_ip_ ? pc_of(cur_ip_) : pc_; }
  bool waiting() const { return waiting_; }    // WAI: state stacked, waiting for an interrupt
  bool sleeping() const { return sleeping_; }  // SLP, HC11 STOP

  ChipModel model() const { return model_; }
  Family family() const { return family_; }

  // --- instruction cache ---------------------------------------------------------
  void invalidate_all();
  void invalidate_range(u32 addr, u32 len);
  // A bank latch changed what the 4 KB-aligned window [base, base+size) shows.
  void select_code_bank(u32 base, u32 size, u32 key);
  // Code may sit in a line the bus does not track (behind a device): a write there.
  void code_written(u32 addr) {
    if (device_code_) invalidate_range(addr & 0xFFFF, 1);
  }
  void code_line_written(u32 addr) override;

  DecodedInsn decode_at(u16 addr) const;

  // --- statistics ----------------------------------------------------------------
  u64 illegal_instructions() const { return illegal_count_; }
  u16 last_illegal_pc() const { return last_illegal_pc_; }
  u64 interrupts_taken() const { return interrupt_count_; }

 private:
  friend struct ExecImpl;

  // --- memory ----------------------------------------------------------------------
  // Data accesses made at cycle `cyc` of the instruction in flight: a device
  // sees now() at that cycle.
  u8 rd8(u16 a, unsigned cyc) {
    if (EMU_LIKELY(!(bus_.attr(a) & Bus::kRdSlow))) return bus_.mem()[a];
    budget_ -= s32(cyc);
    const u8 v = bus_.slow_read8(a);
    budget_ += s32(cyc);
    return v;
  }
  void wr8(u16 a, u8 v, unsigned cyc) {
    if (EMU_LIKELY(!(bus_.attr(a) & Bus::kWrSlow))) { bus_.mem()[a] = v; return; }
    budget_ -= s32(cyc);
    bus_.slow_write8(a, v);
    budget_ += s32(cyc);
  }
  u16 rd16(u16 a, unsigned cyc) { const u8 h = rd8(a, cyc); return u16((h << 8) | rd8(u16(a + 1), cyc + 1)); }
  void wr16(u16 a, u16 v, unsigned cyc) { wr8(a, u8(v >> 8), cyc); wr8(u16(a + 1), u8(v), cyc + 1); }
  void push8(u8 v, unsigned cyc) { wr8(s_, v, cyc); --s_; }
  u8 pull8(unsigned cyc) { ++s_; return rd8(s_, cyc); }
  // PC, (Y,) X, A, B, CC, low byte first: seven pushes from cycle `cyc`,
  // nine on the HC11.
  void push_state(u16 pc, unsigned cyc);
  // A CCR value loaded by TAP or RTI: bits 7-6 read as ones on the MC6801 /
  // HD6301; the HC11's X bit can be cleared this way but not set.
  u8 loaded_ccr(u8 v) const {
    if (family_ != Family::HC11) return u8(v | kCcrFixed);
    return u8((v & ~kX) | (v & cc_ & kX));
  }

  // --- cells -------------------------------------------------------------------------
  const Cell* cell_at(u16 pc) {
    if (EMU_UNLIKELY((pc ^ page_pc_) >> CellPages::kPageShift)) return cells_for(pc);
    return page_cells_ + (pc & (CellPages::kPageSize - 1));
  }
  const Cell* cells_for(u16 pc) {
    page_cells_ = cells_.page(pc);
    page_pc_ = CellPages::page_base(pc);
    return page_cells_ + CellPages::index_in_page(pc);
  }
  u16 pc_of(const Cell* ip) const { return u16(page_pc_ + u32(ip - page_cells_)); }

  // --- interrupts -------------------------------------------------------------------
  void set_i() { cc_ |= kI; if (intc_) intc_->mask_set(); }
  bool take_interrupt();
  bool wake();
  u64 run_slice(s32 states);

  Bus& bus_;
  ChipModel model_;
  Family family_;
  IntcBlock* intc_ = nullptr;

  u8 a_ = 0, b_ = 0, cc_ = kCcrFixed | kI;
  u16 x_ = 0, y_ = 0, s_ = 0, pc_ = 0;
  u16 swi_vector_, trap_vector_;
  // A maskable interrupt is accepted at a boundary at or after this time:
  // clearing I takes effect two cycles after the instruction that did it.
  u64 irq_ok_at_ = 0;
  bool waiting_ = false, sleeping_ = false;
  bool irq_taken_ = false;
  bool device_code_ = false;           // cells decoded from a line the bus does not mark
  u16 trap_lo_ = 1, trap_hi_ = 0;      // HD6301 address error range (empty: lo > hi)
  u64 illegal_count_ = 0;
  u16 last_illegal_pc_ = 0;
  u64 interrupt_count_ = 0;

  static constexpr u32 kNoPage = 0xFFFFFFFFu;
  CellPages cells_;
  Cell* page_cells_ = nullptr;
  u32 page_pc_ = kNoPage;
  const Cell* cur_ip_ = nullptr;
};

}  // namespace m6801

// MCS-96 CPU core: register file, interrupt entry and the execution loop.
//
// Same execution model as the H8/500 core: every guest byte owns a
// pre-decoded Cell, handlers are templates chained with tail calls, a signed
// state budget per slice is the only per-instruction check and the boundary
// conditions (an interrupt) force it negative once.
//
// The architecture puts the working registers in a 256-byte register file
// that data accesses below 0100h select instead of the external bus: the
// zero register at 0, the SFRs at 02h-17h (answered by the chip's peripheral
// block through SfrBlock) and 232 bytes of register RAM from 18h, where the
// stack pointer lives (18h/19h).  Instruction fetches always go to the bus.
#pragma once

#include "common/core.hpp"
#include "cpu/mcs96/bus.hpp"
#include "cpu/mcs96/decode.hpp"
#include "cpu/mcs96/icache.hpp"

namespace mcs96 {

using Scheduler = emu::Scheduler;
using Clock = emu::Clock;

// The on-chip block behind the SFR window and the interrupt logic,
// implemented by the chip's peripherals.
class SfrBlock {
 public:
  struct Interrupt {
    u16 vector = 0;         // vector table address (indirect) or the target itself
    bool maskable = true;   // subject to PSW.I
    bool indirect = true;   // read the target from the vector table
    u8 source = 0xFF;       // pending-bit source for the acknowledge
  };
  virtual ~SfrBlock() = default;
  virtual u8 read_sfr(u8 address) = 0;
  virtual void write_sfr(u8 address, u8 value) = 0;
  virtual void reset_sfrs() = 0;
  // The KB's second PSW word (INT_MASK1 + WSR) saved by PUSHA / POPA.
  virtual u16 aux_psw() const = 0;
  virtual void set_aux_psw(u16 value) = 0;
  // Interrupt arbitration: the request to take at the next boundary, if any
  // (the caller still checks PSW.I for maskable ones), and its acknowledge.
  virtual bool arbitrate(Interrupt& out) const = 0;
  virtual void acknowledge(const Interrupt& irq) = 0;
  // The core entered (true) or left (false) power-down: the block's clock stops.
  virtual void on_power_down(bool entering) = 0;
};

class Cpu final : public emu::SliceCore {
 public:
  struct Regs {
    u16 pc = kResetPc;
    u16 psw = 0;
  };

  // Pending-condition bits.
  static constexpr u32 kPendIrq = 1u << 0;
  static constexpr u32 kPendSleep = 1u << 1;  // IDLPD executed
  static constexpr u32 kPendRefetch = 1u << 2;  // a code bank switched: re-resolve the next cell
  static constexpr u32 kPendHoldoff = 1u << 3;  // the held-off instruction runs alone (see run_slice)

  Cpu(Bus& bus, Variant variant);
  ~Cpu() override;

  // Hardware reset: PC = 2080h, PSW = 0, register RAM all ones, SFRs reset.
  void reset();

  unsigned step();
  u64 run(u64 states);
  u64 poll();

  void set_sfr_block(SfrBlock* b) { sfr_ = b; reeval_irq(); }
  // The chip's pending / mask state changed: re-check for an acceptable request.
  void reeval_irq();

  Regs& regs() { return regs_; }
  const Regs& regs() const { return regs_; }
  Bus& bus() { return bus_; }
  Variant variant() const { return variant_; }
  bool idle() const { return idle_; }
  bool power_down() const { return power_down_; }
  // Leave idle / power-down (an enabled interrupt or a wake-up pin).
  void wake();
  // IDLPD: stop the core; #2 also stops the peripheral clock.
  void sleep(bool power_down);

  // Register file access (data space below 0100h).
  u8 reg8(u8 a) const {
    if (a >= kRegisterRamBase) return ram_[a - kRegisterRamBase];
    return slow_reg8(a);
  }
  u16 reg16(u8 a) const {
    a &= 0xFE;
    if (a >= kRegisterRamBase) return u16(ram_[a - kRegisterRamBase] | (u16(ram_[a - kRegisterRamBase + 1]) << 8));
    return u16(slow_reg8(a) | (u16(slow_reg8(u8(a + 1))) << 8));
  }
  void set_reg8(u8 a, u8 v) {
    if (a >= kRegisterRamBase) ram_[a - kRegisterRamBase] = v;
    else slow_set_reg8(a, v);
  }
  void set_reg16(u8 a, u16 v) {
    a &= 0xFE;
    if (a >= kRegisterRamBase) {
      ram_[a - kRegisterRamBase] = u8(v);
      ram_[a - kRegisterRamBase + 1] = u8(v >> 8);
    } else {
      slow_set_reg8(a, u8(v));
      slow_set_reg8(u8(a + 1), u8(v >> 8));
    }
  }
  u16 sp() const { return reg16(kSpRegister); }
  void set_sp(u16 v) { set_reg16(kSpRegister, v); }
  u8* register_ram() { return ram_; }
  void set_psw(u16 v);

  DecodedInsn decode_at(u16 addr) const;
  void invalidate_all();
  void invalidate_range(u32 addr, u32 len);
  // Banked code window (see emu::CellPages::select_bank): the board pages
  // bank `key` into [base, base+size); each bank keeps its own decoded cells.
  // Safe from within a bus access: the instruction in flight completes and the
  // next one is fetched from the new bank.
  void select_code_bank(u32 base, u32 size, u32 key);
  // Invalidate instructions overlapping an externally written code byte - one
  // the board keeps outside the flat bus, such as a RAM bank in a window.
  void code_written(u16 addr) {
    if (cells_.page_if_allocated(addr)) invalidate_range(addr, 1);
  }
  void code_line_written(u32 addr) override;
  void code_timing_changed() override {}

  u64 illegal_instructions() const { return illegal_count_; }

 private:
  friend struct ExecImpl;

  u8 slow_reg8(u8 a) const;
  void slow_set_reg8(u8 a, u8 v);

  // Data access: register file below 0100h, the bus above.
  u8 read8(u16 a) { return a < kRegisterFileSize ? reg8(u8(a)) : bus_.read8(a); }
  u16 read16(u16 a) { return a < kRegisterFileSize ? reg16(u8(a)) : bus_.read16(a); }
  void write8(u16 a, u8 v) { if (a < kRegisterFileSize) set_reg8(u8(a), v); else bus_.write8(a, v); }
  void write16(u16 a, u16 v) { if (a < kRegisterFileSize) set_reg16(u8(a), v); else bus_.write16(a, v); }
  void push16(u16 v) { const u16 s = u16(sp() - 2); set_sp(s); write16(s, v); }
  u16 pop16() { const u16 s = sp(); const u16 v = read16(s); set_sp(u16(s + 2)); return v; }
  static bool external(u16 a) { return a >= kRegisterFileSize; }

  // Cell of the instruction at `pc`; the page it lies in becomes current
  // (pc_of() is valid for cells of the current page and its guard).
  const Cell* cell_at(u16 pc) {
    if (EMU_UNLIKELY((u32(pc) ^ page_pc_) >> CellPages::kPageShift)) return cells_for(pc);
    return page_cells_ + (pc & (CellPages::kPageSize - 1));
  }
  const Cell* cells_for(u16 pc) {
    page_cells_ = cells_.page(pc);
    page_pc_ = CellPages::page_base(pc);
    return page_cells_ + CellPages::index_in_page(pc);
  }
  u16 pc_of(const Cell* ip) const { return u16(page_pc_ + u32(ip - page_cells_)); }

  // Interrupts stay off for exactly one more instruction: that instruction
  // runs on its own slice so the boundary after it is checked.
  void hold_off() { holdoff_ = true; raise(kPendHoldoff); }

  u64 run_slice(s32 states);
  bool take_interrupt();
  unsigned enter_interrupt(const SfrBlock::Interrupt& irq);
  void reset_architectural_state();

  Bus& bus_;
  Variant variant_;
  SfrBlock* sfr_ = nullptr;
  Regs regs_;
  u8 ram_[kRegisterFileSize - kRegisterRamBase] = {};
  bool holdoff_ = false;  // the next boundary executes an instruction regardless of requests
  bool idle_ = false;
  bool power_down_ = false;
  u64 illegal_count_ = 0;

  static constexpr u32 kNoPage = 0xFFFFFFFFu;  // page_pc_ value matching no address
  CellPages cells_;
  Cell* page_cells_ = nullptr;
  u32 page_pc_ = kNoPage;
};

}  // namespace mcs96

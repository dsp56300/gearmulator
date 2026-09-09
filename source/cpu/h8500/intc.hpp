// Interrupt controller (H8/510 hardware manual section 5, DTE registers
// 6.2.5; H8/570 hardware manual section 5).
//
// Sources: NMI and IRQ pins, plus the internal requests of the on-chip
// modules.  Each module (or IRQ group) has a 3-bit priority level in
// IPRA-IPRD (0 = masked, 7 = highest); NMI is level 8 and always taken.
// Among requests at the same level the fixed order of the chip's Table 5-2
// decides: module order first, then the fixed order within the module.  The
// selected request is presented to the CPU as (level, vector); the CPU takes
// it at the next instruction boundary if level > its interrupt mask.
//
// Sensing: NMI is edge-triggered (edge chosen by NMICR.NMIEG on the H8/510,
// SYSCR1.NMIEG on the H8/570); IRQ0 is level-sensed (low = request) and
// shares its priority with the watchdog interval interrupt; IRQ1-IRQ3 (H8/510
// only) latch on a high-to-low transition and are cleared when the CPU
// accepts them.  IRQn pins are only active while enabled (IRQCR.IRQnE on the
// H8/510, SYSCR1.IRQ0E on the H8/570).
//
// DTEA-DTED select, per source, whether the request starts the data transfer
// controller instead of a CPU interrupt.  Such a request is passed to an
// attached DtcClient if present, else it is served by the CPU.
//
// The set of sources and their registers differ per chip: an IntcLayout
// table describes one chip (which sources exist, their vectors, IPR bits and
// tie-break rank), so the same controller serves the H8/510 and H8/570.
#pragma once
#include <array>

#include "cpu/h8500/bus.hpp"
#include "common/iomux.hpp"
#include "cpu/h8500/cpu.hpp"

namespace h8500 {

// Every interrupt source of the family.  A chip's layout marks the ones it
// does not have as absent.
enum class IrqSrc : u8 {
  Irq0, Wdt,
  Irq1, Irq2, Irq3,
  Frt1Ici, Frt1Ocia, Frt1Ocib, Frt1Fovi,
  Frt2Ici, Frt2Ocia, Frt2Ocib, Frt2Fovi,
  // H8/532 has a third free-running timer
  Frt3Ici, Frt3Ocia, Frt3Ocib, Frt3Fovi,
  TmrCmia, TmrCmib, TmrOvi,
  Sci1Eri, Sci1Rxi, Sci1Txi,
  Sci2Eri, Sci2Rxi, Sci2Txi,
  Adi,
  // H8/570
  PwmOcf0, PwmOcf1, PwmOcf2,
  Isf0, Isf1, Isf2, Isf3, Isf4, Isf5, Isf6, Isf7,
  Isf8, Isf9, Isf10, Isf11, Isf12, Isf13, Isf14, Isf15,
  Count
};

inline IrqSrc isf_src(unsigned n) { return IrqSrc(unsigned(IrqSrc::Isf0) + (n & 15)); }

class DtcClient {
 public:
  virtual ~DtcClient() = default;
  // A request whose DTE bit is set.  Return true if the DTC took it.
  virtual bool dtc_request(IrqSrc src, u8 vector) = 0;
};

// Per-source static description.
struct IrqSrcInfo {
  u8 vector;      // exception vector number (address / 2 in minimum mode)
  u8 ipr;         // IPR register index 0..3 (A..D)
  bool ipr_high;  // level in bits 6-4 (true) or 2-0 (false)
  u8 rank;        // order among equal levels (0 = first); kAbsent = not on this chip
  u8 dte;         // DTE register index 0..3, or 0xFF if the source cannot start the DTC
  u8 dte_bit;
  static constexpr u8 kAbsent = 0xFF;
};

// Register layout of one chip's interrupt controller.  A register address of
// 0 means the chip has no such register (its function is driven from another
// register through set_irq0_enable / set_nmi_edge).
struct IntcLayout {
  u32 ipr_base;   // IPRA..IPRD
  u32 dte_base;   // DTEA..DTED
  u32 nmicr;
  u32 irqcr;
  std::array<IrqSrcInfo, size_t(IrqSrc::Count)> src;
};

const IntcLayout& h8510_intc_layout();
const IntcLayout& h8532_intc_layout();
const IntcLayout& h8570_intc_layout();
const IntcLayout& intc_layout_for(ChipModel model);

class Intc final : public Device, public IrqAckSink {
 public:
  static constexpr u8 kVecNmi = Cpu::kVecNmi;

  Intc(Cpu& cpu, const IntcLayout& layout);
  ~Intc() override;

  void map(emu::IoMux& mux);
  void reset();

  // --- module side ----------------------------------------------------------
  // Level-sensitive request from an on-chip module (its flag bit AND enable).
  void set_request(IrqSrc src, bool active);
  bool request(IrqSrc src) const { return req_[size_t(src)]; }
  void set_dtc_client(DtcClient* c) { dtc_ = c; }

  // --- pin side -------------------------------------------------------------
  // IRQn pin level (true = low = asserted).  n = 0..3.
  void set_irq_pin(unsigned n, bool low);
  // NMI pin level.
  void set_nmi_pin(bool high);
  // For chips whose enables live outside this controller: the H8/570 keeps
  // IRQ0E in SYSCR1, the H8/532 keeps IRQ0 / IRQ1 in P1CR.
  void set_irq0_enable(bool on);
  void set_irq_enable(unsigned n, bool on);
  void set_nmi_edge(bool rising) { nmicr_ = u8(0xFE | (rising ? 1 : 0)); }

  // --- register access ------------------------------------------------------
  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  // --- CPU acknowledge ------------------------------------------------------
  void irq_acknowledged(u8 vector) override;

  // A one-shot CPU interrupt for `src` regardless of its DTE bit and module
  // flag: the DTC raises it when a transfer count reaches zero.  Cleared on
  // acceptance.
  void raise_cpu_interrupt(IrqSrc src);

  // Introspection.
  bool present(IrqSrc src) const { return lay_.src[size_t(src)].rank != IrqSrcInfo::kAbsent; }
  u8 level_of(IrqSrc src) const;
  bool dtc_enabled(IrqSrc src) const;
  u8 ipr(unsigned i) const { return ipr_[i]; }
  u8 dte(unsigned i) const { return dte_[i]; }
  u8 nmicr() const { return nmicr_; }
  u8 irqcr() const { return irqcr_; }

 private:
  void update();
  bool irq_enabled(unsigned n) const { return (irqcr_ >> n) & 1; }

  Cpu& cpu_;
  const IntcLayout& lay_;
  DtcClient* dtc_ = nullptr;
  std::array<bool, size_t(IrqSrc::Count)> req_{};
  std::array<bool, size_t(IrqSrc::Count)> latched_{};  // raise_cpu_interrupt()
  bool irq_pin_low_[4] = {};
  bool nmi_pin_high_ = true;
  u8 ipr_[4] = {};
  u8 dte_[4] = {};
  u8 nmicr_ = 0xFE;
  u8 irqcr_ = 0xF0;
};

}  // namespace h8500

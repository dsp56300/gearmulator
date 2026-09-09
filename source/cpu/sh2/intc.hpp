// Interrupt controller of the SH7014 family (hardware manual section 6), of
// the SH7042 (SH7040 manual section 6) and of the SH7034 (SH7032/34 manual
// section 5).
//
// Sources: NMI (edge, level 16), the IRQn pins (level or falling edge per
// ICR), and the on-chip module requests.  Priority: the IPR nibble of the
// source (0 = masked), then the chip's default order among equals.  The
// winner is presented to the CPU as (level, vector); the CPU accepts it when
// the level exceeds SR.I3-I0.  Edge-latched IRQ requests are cleared when
// accepted or, on the SH7014, when software writes 0 to their ISR flag after
// reading 1.  Which sources exist, their vectors, IPR fields and register
// addresses come from an IntcLayout, so the same class and the same IrqSrc
// names serve both chips (the ITU's IMIA/IMIB/OVI are the MTU's TGIA/TGIB/
// TCIV here).
#pragma once
#include <array>

#include "common/iomux.hpp"
#include "cpu/sh2/cpu.hpp"

namespace sh2 {

// Every source of either chip.  The first block is the SH7014's table 6.3 in
// default priority order; the SH7034-only sources follow.
enum class IrqSrc : u8 {
  Irq0, Irq1, Irq2, Irq3, Irq6, Irq7,
  Dei0, Dei1,
  Tgi0a, Tgi0b, Tgi0c, Tgi0d, Tci0v,
  Tgi1a, Tgi1b, Tci1v, Tci1u,
  Tgi2a, Tgi2b, Tci2v, Tci2u,
  Eri0, Rxi0, Txi0, Tei0,
  Eri1, Rxi1, Txi1, Tei1,
  Adi,
  Cmi0, Cmi1,
  Iti, Cmi,
  Irq4, Irq5, Dei2, Dei3,
  Tgi3a, Tgi3b, Tci3v, Tgi4a, Tgi4b, Tci4v,
  Pei,
  Tgi3c, Tgi3d, Tgi4c, Tgi4d, Adi1, Swdtend, Oei,
  kCount
};

struct IrqSrcInfo {
  u8 vector;  // 0: the source does not exist on this chip
  u8 ipr;     // IPR register index
  u8 shift;   // nibble position within the IPR (12, 8, 4, 0)
  u8 rank;    // default priority order among equal levels (0 = highest)
};

struct IntcLayout {
  std::array<IrqSrcInfo, size_t(IrqSrc::kCount)> src;
  u32 ipr_base;    // IPRA; IPRB.. follow at +2
  u8 ipr_count;
  u32 icr;         // ICR: NMIL bit 15, NMIE bit 8, IRQnS sense bits per pin_bit
  u32 isr;         // ISR (SH7014 only), 0 = none
  u16 icr_mask;    // writable ICR bits
  std::array<u8, 8> pin_bit;      // ICR / ISR bit of IRQn (0xFF = pin absent)
  std::array<IrqSrc, 8> pin_src;  // source of IRQn
};
const IntcLayout& intc_layout(ChipModel model);

// Receives module requests routed to the DMAC instead of the CPU.
class DmaRequestClient {
 public:
  virtual ~DmaRequestClient() = default;
  virtual void dma_request(IrqSrc src) = 0;
};

class Intc final : public Device, public IrqAckSink {
 public:
  Intc(Cpu& cpu, const IntcLayout& layout);

  void map(emu::IoMux& mux);
  void reset();

  // --- module side ----------------------------------------------------------
  void set_request(IrqSrc src, bool active);
  bool request(IrqSrc src) const { return req_[size_t(src)]; }
  // Route a module source to the DMAC (masks it from the CPU while set).
  void set_dma_route(IrqSrc src, bool on) { dma_route_[size_t(src)] = on; update(); }
  void set_dma_client(DmaRequestClient* c) { dma_ = c; }

  // --- pin side -------------------------------------------------------------
  void set_irq_pin(unsigned n, bool low);  // n = 0..7 (the chip's pins); true = low
  bool set_nmi_pin(bool high);  // returns true when the edge requested an NMI

  // --- registers --------------------------------------------------------------
  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;
  void write16(u32 addr, u16 value) override;

  void irq_acknowledged(u8 vector) override;

  const IntcLayout& layout() const { return layout_; }
  u16 ipr(unsigned i) const { return ipr_[i & 7]; }
  u16 icr() const { return u16(icr_ | (nmi_high_ ? 0x8000 : 0)); }
  u16 isr() const { return isr_; }
  u8 level_of(IrqSrc s) const { const IrqSrcInfo& i = layout_.src[size_t(s)]; return u8((ipr_[i.ipr] >> i.shift) & 0xF); }
  u8 vector_of(IrqSrc s) const { return layout_.src[size_t(s)].vector; }

 private:
  void update();
  u16 pin_mask(unsigned n) const { return layout_.pin_bit[n] == 0xFF ? 0 : u16(1u << layout_.pin_bit[n]); }
  void resample_pins();

  Cpu& cpu_;
  const IntcLayout& layout_;
  DmaRequestClient* dma_ = nullptr;
  std::array<u16, 8> ipr_{};
  u16 icr_ = 0;
  u16 isr_ = 0;  // IRQ request flags at the pin_bit positions (edge latches)
  bool nmi_high_ = true;
  std::array<bool, 8> pin_low_{};
  std::array<bool, size_t(IrqSrc::kCount)> req_{};
  std::array<bool, size_t(IrqSrc::kCount)> dma_route_{};
};

}  // namespace sh2

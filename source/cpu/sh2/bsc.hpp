// SH7014 bus state controller (hardware manual section 8).
//
// Registers (all 16-bit, 8/16/32-bit access, table 8.2):
//   H'FFFF8620 BCR1   -  -  1  -  -  -  -  IOE | -  -  -  -  A3SZ A2SZ A1SZ A0SZ   (H'200F)
//   H'FFFF8622 BCR2   IW31 IW30 IW21 IW20 IW11 IW10 IW01 IW00 | CW3-CW0 SW3-SW0  (H'FFFF)
//   H'FFFF8624 WCR1   W33-W30 W23-W20 | W13-W10 W03-W00                          (H'FFFF)
//   H'FFFF8626 WCR2   - ... - | -  -  DDW1 DDW0 DSW3 DSW2 DSW1 DSW0               (H'000F)
//   H'FFFF862A DCR    TPC RCD TRAS1 TRAS0 DWW1 DWW0 DWR1 DWR0 | DIW - BE RASD - SZ0 AMX1 AMX0
//   H'FFFF862C RTCSR  - ... - | -  CMF  CMIE CKS2 CKS1 CKS0 RFSH RMD
//   H'FFFF862E RTCNT  8-bit up counter (bits 15-8 read 0)
//   H'FFFF8630 RTCOR  8-bit compare constant (bits 15-8 read 0)
//
// What is modelled:
//   * The registers with their reserved-bit rules and power-on values (8.2).
//   * Retiming of the external areas: every write to BCR1 / WCR1 / WCR2 / DCR
//     recomputes the Bus AccessClass of CS0-CS3 (Bus::kClsCs0..kClsCs3) and
//     DRAM (Bus::kClsDram): bus width from A3SZ-A0SZ (CS0 in on-chip-ROM-
//     disabled modes takes its width from the mode pin, 8.2.1) and from DCR.SZ0,
//     software wait states from WCR1 and from DCR (8.3.2, 8.4.3).
//   * The refresh timer as an interval timer (8.2.6-8.2.8): RTCNT is a lazy
//     8-bit counter clocked by the CKS2-0 prescaler, compared with RTCOR; on
//     the match it clears, CMF is set and, with CMIE, the CMI interrupt
//     (vector 153) is requested through the Intc.  One scheduler event covers
//     the next interrupt-capable match (same pattern as the H8 TMR).
//
// Approximations, all documented at the point of use in bsc.cpp:
//   * Idle cycles between accesses (BCR2 IW/CW, DCR DIW, 8.6) and the CS
//     assert extension (BCR2 SW, 8.3.3) depend on the pair of consecutive bus
//     cycles, which an AccessClass cannot express: they are stored and exposed
//     but not charged.
//   * The WAIT pin (external waits, 8.3.2) is not modelled.
//   * DRAM: RAS-up-mode timing only (Tp Tr Tc1 Tc2 + waits, figure 8.7); the
//     read wait count DWR is used for both directions; high-speed page mode /
//     RAS down mode (8.4.4) are not modelled.  Refresh cycles themselves
//     (CAS-before-RAS, self-refresh, 8.4.5) are not modelled: they cost no bus
//     time and DRAM contents never decay; the number of refresh requests is
//     counted for boards that want to check the refresh interval.
//   * CS3 multiplexed I/O space (8.5): the per-access width selected by A14
//     cannot be expressed per class; A3SZ is used and the fixed address-output
//     cycles (Ta1-Ta4, figure 8.17) are added to the wait count.
//   * WCR2 (DMA single-address waits) has no effect on CPU accesses; it is
//     stored and exposed for the DMAC.
#pragma once
#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/chip.hpp"
#include "cpu/sh2/intc.hpp"

namespace sh2 {

class Bsc final : public Device {
 public:
  static constexpr u32 kBcr1 = 0xFFFF8620u, kBcr2 = 0xFFFF8622u, kWcr1 = 0xFFFF8624u, kWcr2 = 0xFFFF8626u;
  static constexpr u32 kDcr = 0xFFFF862Au, kRtcsr = 0xFFFF862Cu, kRtcnt = 0xFFFF862Eu, kRtcor = 0xFFFF8630u;

  // Power-on values (table 8.2).
  static constexpr u16 kBcr1Init = 0x200F, kBcr2Init = 0xFFFF, kWcr1Init = 0xFFFF, kWcr2Init = 0x000F;

  // BCR1 bits
  static constexpr u16 kIoe = 0x0100;
  // DCR bits
  static constexpr u16 kTpc = 0x8000, kRcd = 0x4000, kDiw = 0x0080, kBe = 0x0020, kRasd = 0x0010, kSz0 = 0x0004;
  // RTCSR bits
  static constexpr u16 kCmf = 0x0040, kCmie = 0x0020, kRfsh = 0x0002, kRmd = 0x0001;

  // Basic ordinary-space cycle as seen by the core's access model: the T1/T2
  // pair of figure 8.3 (the core charges a data access `cycles - 1` states,
  // the one state its MA stage overlaps) plus the WCR1 waits.
  static constexpr u8 kOrdinaryBase = 2;  // T1 + T2 (figure 8.3)
  // States of a DRAM cycle beyond an ordinary one, before waits: Tp and Tr
  // (figure 8.7, RAS up mode: every access reopens the row).
  static constexpr u8 kDramOverhead = 1;  // Tp + Tr + Tc1 + Tc2 = 4 with the 2-state base
  // Fixed address-output cycles of a multiplexed I/O access (figure 8.17).
  static constexpr u8 kMuxIoAddressCycles = 4;

  Bsc(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, Bus& bus, const ChipConfig& cfg);
  ~Bsc() override;

  void map(emu::IoMux& mux);
  // Power-on reset: register values of table 8.2 and the default access
  // classes (16-bit, 15 waits for CS0-CS3 -- CS0 width from the mode pin --
  // and 8-bit, no wait for DRAM).
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;
  void write16(u32 addr, u16 value) override;

  // --- raw register values ------------------------------------------------
  u16 bcr1() const { return bcr1_; }
  u16 bcr2() const { return bcr2_; }
  u16 wcr1() const { return wcr1_; }
  u16 wcr2() const { return wcr2_; }
  u16 dcr() const { return dcr_; }
  u16 rtcsr() { sync(clock_.now()); return rtcsr_; }
  u8 rtcnt() { sync(clock_.now()); return rtcnt_; }
  u8 rtcor() const { return rtcor_; }

  // --- decoded settings (area = 0..3 for CS0..CS3) --------------------------
  unsigned cs_width(unsigned area) const;        // 8 or 16 (8.2.1; CS0 per the mode pin when ROM is disabled)
  unsigned cs_wait(unsigned area) const { return (wcr1_ >> (4 * (area & 3))) & 0xF; }  // W bits (8.2.3)
  unsigned cs_idle(unsigned area) const { return (bcr2_ >> (8 + 2 * (area & 3))) & 3; }  // IW bits (8.6.1)
  bool cs_continuous_idle(unsigned area) const { return (bcr2_ >> (4 + (area & 3))) & 1; }  // CW bits (8.6.2)
  bool cs_assert_extension(unsigned area) const { return (bcr2_ >> (area & 3)) & 1; }  // SW bits (8.3.3)
  bool multiplex_io() const { return (bcr1_ & kIoe) != 0; }  // CS3 is address/data multiplexed I/O space (8.5)
  unsigned dram_width() const { return (dcr_ & kSz0) ? 16 : 8; }
  unsigned dram_read_wait() const { return (dcr_ >> 8) & 3; }   // DWR
  unsigned dram_write_wait() const { return (dcr_ >> 10) & 3; } // DWW
  unsigned dram_ras_precharge() const { return (dcr_ & kTpc) ? 2 : 1; }  // TPC: 1.5 / 2.5 cycles, rounded
  unsigned dram_ras_cas_delay() const { return (dcr_ & kRcd) ? 2 : 1; }  // RCD
  unsigned dram_refresh_ras_cycles() const { return ((dcr_ >> 12) & 3) + 2; }  // TRAS: 2.5 .. 5.5, rounded down
  bool dram_idle() const { return (dcr_ & kDiw) != 0; }
  bool dram_burst() const { return (dcr_ & kBe) != 0; }
  bool dram_ras_down() const { return (dcr_ & kRasd) != 0; }
  unsigned dram_row_bits() const { return 9 + (dcr_ & 3); }  // AMX (table 8.4)
  unsigned dma_cs_wait() const { return wcr2_ & 0xF; }         // DSW (8.2.4)
  unsigned dma_dram_wait() const { return (wcr2_ >> 4) & 3; }  // DDW (8.2.4)
  bool refresh_enabled() const { return (rtcsr_ & kRfsh) != 0; }
  bool self_refresh() const { return (rtcsr_ & (kRfsh | kRmd)) == (kRfsh | kRmd); }
  // Prescaler shift of the refresh timer clock (0 = stopped), table of 8.2.6.
  unsigned refresh_clock_shift() const;
  // Number of CAS-before-RAS refresh requests generated so far (matches with
  // RFSH = 1, RMD = 0); the refresh cycles themselves are not modelled.
  u64 refresh_requests() { sync(clock_.now()); return refreshes_; }

 private:
  static void on_event(void* self, u64 when, u64 now);
  u16 read_reg(u32 reg);           // raw 16-bit value, no side effects
  void write_reg(u32 reg, u16 v);  // full 16-bit write with side effects
  void install_classes();
  u64 tick_of(u64 t) const { return t >> refresh_clock_shift(); }
  void sync(u64 now);
  void update_requests();
  void reschedule();

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  Bus& bus_;
  const ChipConfig& cfg_;
  emu::Scheduler::EventId event_ = 0;

  u16 bcr1_ = kBcr1Init, bcr2_ = kBcr2Init, wcr1_ = kWcr1Init, wcr2_ = kWcr2Init, dcr_ = 0;
  u16 rtcsr_ = 0;
  bool cmf_read_ = false;  // CMF was read as 1 (clear condition, 8.2.6)
  u8 rtcnt_ = 0, rtcor_ = 0;
  u64 tick_ = 0;   // prescaler ticks accounted for
  u64 now_ = 0;    // time of the last sync
  u64 refreshes_ = 0;
};

}  // namespace sh2

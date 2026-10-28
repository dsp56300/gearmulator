#include "cpu/sh3/periph.hpp"

#include <algorithm>

namespace sh3 {

namespace {
constexpr u32 kArea1Base = 0x04000000u;  // physical
constexpr u32 kArea1Size = 0x1000u;
constexpr u32 kP4Base = 0xFFFFFE80u, kP4Size = 0x180u;
constexpr u32 kSdmrBase = 0xFFFFD000u, kSdmrSize = 0x2000u;

// INTEVT2 codes (SH7709A table 6.5) per source; the IRL level is computed.
constexpr u32 kIntevt2[kSourceCount] = {
    0x1C0,                                     // NMI
    0x600, 0x620, 0x640, 0x660, 0x680, 0x6A0,  // IRQ0-5
    0x000,                                     // IRL level: computed
    0x700, 0x720,                              // PINT0-7, PINT8-15
    0x800, 0x820, 0x840, 0x860,                // DEI0-3
    0x880, 0x8A0, 0x8C0, 0x8E0,                // IrDA ERI1, RXI1, BRI1, TXI1
    0x900, 0x920, 0x940, 0x960,                // SCIF ERI2, RXI2, BRI2, TXI2
    0x980,                                     // ADI
    0x400, 0x420, 0x440, 0x460,                // TUNI0-2, TICPI2
    0x480, 0x4A0, 0x4C0,                       // ATI, PRI, CUI
    0x4E0, 0x500, 0x520, 0x540,                // SCI ERI, RXI, TXI, TEI
    0x560, 0x580, 0x5A0,                       // ITI, RCMI, ROVI
};

constexpr u32 kTmuDiv[8] = {4, 16, 64, 256, 1024, 1, 1, 1};
constexpr u32 kDmaSize[4] = {1, 2, 4, 16};
constexpr u32 kChcrDE = 1u << 0, kChcrTE = 1u << 1, kChcrIE = 1u << 2;
constexpr u16 kDmaorDME = 1u << 0, kDmaorNMIF = 1u << 1, kDmaorAE = 1u << 2;

// SCI SSR bits.
constexpr u8 kSsrTdre = 0x80, kSsrRdrf = 0x40, kSsrErrors = 0x38, kSsrTend = 0x04;
// SCR bits.
constexpr u8 kScrTie = 0x80, kScrRie = 0x40, kScrTe = 0x20, kScrRe = 0x10, kScrTeie = 0x04;
// SCIF SSR bits.
constexpr u16 kFsrEr = 0x80, kFsrTend = 0x40, kFsrTdfe = 0x20, kFsrBrk = 0x10, kFsrFer = 0x08, kFsrPer = 0x04,
              kFsrRdf = 0x02, kFsrDr = 0x01;

// Register-access helpers for the dispatch switches: succeed only at the
// register's natural width.
#define SH3_R(width, expr)                     \
  do {                                         \
    if (size != (width)) return false;         \
    v = u32(expr);                             \
    return true;                               \
  } while (0)
#define SH3_W(width, stmt)                     \
  do {                                         \
    if (size != (width)) return false;         \
    { stmt; }                                  \
    return true;                               \
  } while (0)
}  // namespace

Peripherals::Peripherals(emu::Scheduler& sched, Cpu& cpu, Bus& bus, u64 cpu_clock_hz, u16 frqcr_reset)
    : sched_(sched), cpu_(cpu), bus_(bus), cpu_clock_hz_(cpu_clock_hz), frqcr_reset_(frqcr_reset),
      devs_{RegDevice(*this, kRegs), RegDevice(*this, kSdmr)} {
  apply_frqcr(frqcr_reset);
  for (unsigned i = 0; i < 4; ++i) { dma_[i].owner = this; dma_[i].index = i; }
  for (unsigned i = 0; i < 3; ++i) { tmu_[i].owner = this; tmu_[i].index = i; }
  sci_.owner = this;
  for (unsigned i = 0; i < 2; ++i) { scif_[i].owner = this; scif_[i].index = i; }
  cpu_.set_intc(this);
}

Peripherals::~Peripherals() {
  cpu_.set_intc(nullptr);
  for (auto& t : tmu_) cancel(t.event);
  for (auto& d : dma_) cancel(d.event);
  cancel(sci_.rx_event);
  for (auto& s : scif_) cancel(s.rx_event);
  cancel(wdt_event_);
  cancel(refresh_event_);
  cancel(adc_event_);
}

void Peripherals::map(Bus& bus) {
  bus.map_device(kP4Base, kP4Size, &devs_[kRegs]);
  bus.set_noexec(kP4Base, kP4Size);
  // SDRAM mode register windows: the address is the data.
  bus.map_device(kSdmrBase, kSdmrSize, &devs_[kSdmr]);
  bus.map_physical_device(kArea1Base, kArea1Size, &devs_[kRegs], Bus::kClsRegs);
  // Cache address / data arrays (16 MB windows at F0000000 / F1000000):
  // plain storage for the firmware's invalidation writes.
  bus.map_ram(0xF0000000u, Bus::kPageSize);
  bus.mirror(0xF0000000u + Bus::kPageSize, 0x01000000u - Bus::kPageSize, 0xF0000000u);
  bus.map_ram(0xF1000000u, Bus::kPageSize);
  bus.mirror(0xF1000000u + Bus::kPageSize, 0x01000000u - Bus::kPageSize, 0xF1000000u);
}

// FRQCR (section 10): STC2-0 (bits 9, 5-4) multiply the bus clock, IFC1-0
// (bits 3-2) divide the result for the CPU clock and PFC1-0 (bits 1-0) for
// the peripheral clock (00 = 1, 01 = 1/2, 10 = 1/4; 11 is prohibited).  Only
// the ratios matter here: a peripheral clock is PFC / IFC CPU states long.
void Peripherals::apply_frqcr(u16 v) {
  static constexpr u32 div[4] = {1, 2, 4, 4};
  static constexpr u32 mult[8] = {1, 2, 3, 4, 6, 8, 1, 1};
  frqcr_ = v;
  const u32 stc = mult[((v >> 4) & 3) | ((v >> 7) & 4)], ifc = div[(v >> 2) & 3], pfc = div[v & 3];
  pclk_div_ = std::max(1u, pfc / ifc);
  // The bus clock is the PLL input (CKIO): STC / IFC CPU states long.
  bclk_div_ = std::max(1u, stc / ifc);
}

void Peripherals::reset() {
  requesting_ = 0;
  icr0_ = 0; icr1_ = 0x4000; icr2_ = 0; pinter_ = 0;
  for (auto& p : ipr_) p = 0;
  irr0_ = 0;
  irl_level_ = 15;
  for (auto& p : irq_high_) p = true;
  recompute_priorities();
  pteh_ = ptel_ = ttb_ = tea_ = mmucr_ = ccr_ = ccr2_ = 0;
  basra_ = basrb_ = 0;
  for (auto& v : ubc_) v = 0;
  sdir_ = 0xFFFF;
  bcr1_ = 0; bcr2_ = 0x3FF0; bcr3_ = 0; wcr1_ = 0x3FF3; wcr2_ = 0xFFFF; mcr_ = 0; dcr_ = 0; bsc_pcr_ = 0;
  rtcsr_ = 0; rtcnt_ = 0; rtcor_ = 0; rfcr_ = 0;
  cancel(refresh_event_);
  refresh_synced_ = now(); refresh_presc_ = 0;
  for (auto& d : dma_) { cancel(d.event); d.sar = d.dar = d.tcr = d.chcr = 0; }
  dmaor_ = 0;
  apply_frqcr(frqcr_reset_);
  stbcr_ = stbcr2_ = 0; wtcsr_ = 0; wtcnt_ = 0;
  cancel(wdt_event_);
  wdt_synced_ = now(); wdt_presc_ = 0;
  for (auto& v : rtc_) v = 0;
  rtc_[7] = 0x00; rcr1_ = 0; rcr2_ = 0x09; rtc_synced_ = now(); rtc_frac_ = 0;
  tocr_ = tstr_ = 0;
  for (unsigned i = 0; i < 3; ++i) {
    cancel(tmu_[i].event);
    tmu_[i].tcor = tmu_[i].tcnt = 0xFFFFFFFFu;
    tmu_[i].tcr = 0;
    tmu_[i].synced = now();
    tmu_[i].presc = 0;
  }
  tcpr2_ = 0;
  cmstr_ = 0; cmcsr_ = 0; cmcnt_ = 0; cmcor_ = 0xFFFF; cmt_synced_ = now(); cmt_presc_ = 0;
  adcsr_ = 0; adcr_ = 0x07;
  for (auto& a : addr_) a = 0;
  cancel(adc_event_);
  dadr_[0] = dadr_[1] = 0; dacr_ = 0x1F;
  for (auto& p : pcr_) p = 0;
  for (auto& p : pdr_) p = 0;
  for (Serial* s : {&sci_, &scif_[0], &scif_[1]}) {
    cancel(s->rx_event);
    s->smr = 0; s->brr = 0xFF; s->scr = 0; s->tdr = 0xFF; s->ssr = 0x84; s->rdr = 0; s->scmr = 0;
    s->fsr = 0x0060; s->fcr = 0; s->fdr = 0;
    s->queue.clear(); s->fifo.clear(); s->rx_active = false;
  }
  update_irq();
}

// ---------------------------------------------------------------------------
// Register access: width composition (big-endian) over the natural registers

bool Peripherals::reg_read(u32 a, unsigned size, u32& v) {
  if (a >= kP4Base) return p4_read(a, size, v);
  const u32 phys = a & Bus::kPhysMask;
  if (phys >= kArea1Base && phys < kArea1Base + kArea1Size) return area1_read(phys - kArea1Base, size, v);
  return false;
}

bool Peripherals::reg_write(u32 a, unsigned size, u32 v) {
  if (a >= kP4Base) return p4_write(a, size, v);
  const u32 phys = a & Bus::kPhysMask;
  if (phys >= kArea1Base && phys < kArea1Base + kArea1Size) return area1_write(phys - kArea1Base, size, v);
  return false;
}

u8 Peripherals::reg_read8(Module m, u32 a) {
  u32 v = 0;
  if (m == kSdmr) return 0xFF;
  if (reg_read(a, 1, v)) {}
  else if (reg_read(a & ~1u, 2, v)) v >>= (a & 1) ? 0 : 8;
  else if (reg_read(a & ~3u, 4, v)) v >>= 8 * (3 - (a & 3));
  else v = 0;
  return u8(v);
}

u16 Peripherals::reg_read16(Module m, u32 a) {
  u32 v = 0;
  a &= ~1u;
  if (m == kSdmr) return 0xFFFF;
  if (reg_read(a, 2, v)) {}
  else if (reg_read(a & ~3u, 4, v)) v >>= (a & 2) ? 0 : 16;
  else v = (u32(reg_read8(m, a)) << 8) | reg_read8(m, a + 1);
  return u16(v);
}

u32 Peripherals::reg_read32(Module m, u32 a) {
  u32 v = 0;
  a &= ~3u;
  if (m == kSdmr) return 0xFFFFFFFFu;
  if (!reg_read(a, 4, v)) v = (u32(reg_read16(m, a)) << 16) | reg_read16(m, a + 2);
  return v;
}

void Peripherals::reg_write8(Module m, u32 a, u8 v) {
  if (m == kSdmr) return;
  if (reg_write(a, 1, v)) return;
  u32 old;
  if (reg_read(a & ~1u, 2, old)) {
    const unsigned sh = (a & 1) ? 0 : 8;
    reg_write(a & ~1u, 2, (old & ~(0xFFu << sh)) | (u32(v) << sh));
  } else if (reg_read(a & ~3u, 4, old)) {
    const unsigned sh = 8 * (3 - (a & 3));
    reg_write(a & ~3u, 4, (old & ~(0xFFu << sh)) | (u32(v) << sh));
  }
}

void Peripherals::reg_write16(Module m, u32 a, u16 v) {
  if (m == kSdmr) return;
  a &= ~1u;
  if (reg_write(a, 2, v)) return;
  u32 old;
  if (reg_read(a & ~3u, 4, old)) {
    const unsigned sh = (a & 2) ? 0 : 16;
    reg_write(a & ~3u, 4, (old & ~(0xFFFFu << sh)) | (u32(v) << sh));
    return;
  }
  reg_write(a, 1, v >> 8);
  reg_write(a + 1, 1, v & 0xFF);
}

void Peripherals::reg_write32(Module m, u32 a, u32 v) {
  if (m == kSdmr) return;
  a &= ~3u;
  if (reg_write(a, 4, v)) return;
  if (!reg_write(a, 2, v >> 16)) { reg_write(a, 1, v >> 24); reg_write(a + 1, 1, (v >> 16) & 0xFF); }
  if (!reg_write(a + 2, 2, v & 0xFFFF)) { reg_write(a + 2, 1, (v >> 8) & 0xFF); reg_write(a + 3, 1, v & 0xFF); }
}

// ---------------------------------------------------------------------------
// P4 registers (SCI, TMU, RTC, INTC, BSC, CPG, UBC, CCN)

bool Peripherals::p4_read(u32 a, unsigned size, u32& v) {
  switch (a) {
    // SCI
    case 0xFFFFFE80: SH3_R(1, sci_.smr);
    case 0xFFFFFE82: SH3_R(1, sci_.brr);
    case 0xFFFFFE84: SH3_R(1, sci_.scr);
    case 0xFFFFFE86: SH3_R(1, sci_.tdr);
    case 0xFFFFFE88: SH3_R(1, sci_.ssr | kSsrTdre | kSsrTend);  // the transmitter never lags
    case 0xFFFFFE8A: SH3_R(1, sci_.rdr);
    case 0xFFFFFE8C: SH3_R(1, sci_.scmr);
    // TMU
    case 0xFFFFFE90: SH3_R(1, tocr_);
    case 0xFFFFFE92: SH3_R(1, tstr_);
    case 0xFFFFFE94: SH3_R(4, tmu_[0].tcor);
    case 0xFFFFFE98: tmu_sync(0, now()); SH3_R(4, tmu_[0].tcnt);
    case 0xFFFFFE9C: SH3_R(2, tmu_[0].tcr);
    case 0xFFFFFEA0: SH3_R(4, tmu_[1].tcor);
    case 0xFFFFFEA4: tmu_sync(1, now()); SH3_R(4, tmu_[1].tcnt);
    case 0xFFFFFEA8: SH3_R(2, tmu_[1].tcr);
    case 0xFFFFFEAC: SH3_R(4, tmu_[2].tcor);
    case 0xFFFFFEB0: tmu_sync(2, now()); SH3_R(4, tmu_[2].tcnt);
    case 0xFFFFFEB4: SH3_R(2, tmu_[2].tcr);
    case 0xFFFFFEB8: SH3_R(4, tcpr2_);
    // RTC
    case 0xFFFFFEC0: rtc_sync(); SH3_R(1, rtc_[0] >> 1);
    case 0xFFFFFEC2: rtc_sync(); SH3_R(1, rtc_[1]);
    case 0xFFFFFEC4: rtc_sync(); SH3_R(1, rtc_[2]);
    case 0xFFFFFEC6: rtc_sync(); SH3_R(1, rtc_[3]);
    case 0xFFFFFEC8: rtc_sync(); SH3_R(1, rtc_[4]);
    case 0xFFFFFECA: rtc_sync(); SH3_R(1, rtc_[5]);
    case 0xFFFFFECC: rtc_sync(); SH3_R(1, rtc_[6]);
    case 0xFFFFFECE: rtc_sync(); SH3_R(1, rtc_[7]);
    case 0xFFFFFED0: SH3_R(1, rtc_[8]);
    case 0xFFFFFED2: SH3_R(1, rtc_[9]);
    case 0xFFFFFED4: SH3_R(1, rtc_[10]);
    case 0xFFFFFED6: SH3_R(1, rtc_[11]);
    case 0xFFFFFED8: SH3_R(1, rtc_[12]);
    case 0xFFFFFEDA: SH3_R(1, rtc_[13]);
    case 0xFFFFFEDC: SH3_R(1, rcr1_);
    case 0xFFFFFEDE: SH3_R(1, rcr2_);
    // INTC
    case 0xFFFFFEE0: SH3_R(2, icr0());
    case 0xFFFFFEE2: SH3_R(2, ipr_[0]);
    case 0xFFFFFEE4: SH3_R(2, ipr_[1]);
    // BSC
    case 0xFFFFFF60: SH3_R(2, bcr1_);
    case 0xFFFFFF62: SH3_R(2, bcr2_);
    case 0xFFFFFF64: SH3_R(2, wcr1_);
    case 0xFFFFFF66: SH3_R(2, wcr2_);
    case 0xFFFFFF68: SH3_R(2, mcr_);
    case 0xFFFFFF6A: SH3_R(2, dcr_);
    case 0xFFFFFF6C: SH3_R(2, bsc_pcr_);
    case 0xFFFFFF6E: refresh_sync(); SH3_R(2, rtcsr_);
    case 0xFFFFFF70: refresh_sync(); SH3_R(2, rtcnt_);
    case 0xFFFFFF72: SH3_R(2, rtcor_);
    case 0xFFFFFF74: refresh_sync(); SH3_R(2, rfcr_);
    case 0xFFFFFF7E: SH3_R(2, bcr3_);
    // CPG / WDT
    case 0xFFFFFF80: SH3_R(2, frqcr_);
    case 0xFFFFFF82: SH3_R(1, stbcr_);
    case 0xFFFFFF84: wdt_sync(now()); SH3_R(1, wtcnt_);
    case 0xFFFFFF86: wdt_sync(now()); SH3_R(1, wtcsr_);
    case 0xFFFFFF88: SH3_R(1, stbcr2_);
    // UBC (stored)
    case 0xFFFFFF90: SH3_R(4, ubc_[0]);
    case 0xFFFFFF94: SH3_R(4, ubc_[1]);
    case 0xFFFFFF98: SH3_R(2, ubc_[2]);
    case 0xFFFFFF9C: SH3_R(2, ubc_[3]);
    case 0xFFFFFFA0: SH3_R(4, ubc_[4]);
    case 0xFFFFFFA4: SH3_R(1, ubc_[5]);
    case 0xFFFFFFA8: SH3_R(2, ubc_[6]);
    case 0xFFFFFFAC: SH3_R(4, ubc_[7]);
    case 0xFFFFFFB0: SH3_R(4, ubc_[8]);
    case 0xFFFFFFB4: SH3_R(1, ubc_[9]);
    case 0xFFFFFFB8: SH3_R(2, ubc_[10]);
    case 0xFFFFFFBC: SH3_R(4, ubc_[11]);
    // CCN
    case 0xFFFFFFD0: SH3_R(4, cpu_.tra());
    case 0xFFFFFFD4: SH3_R(4, cpu_.expevt());
    case 0xFFFFFFD8: SH3_R(4, cpu_.intevt());
    case 0xFFFFFFE0: SH3_R(4, mmucr_);
    case 0xFFFFFFE4: SH3_R(1, basra_);
    case 0xFFFFFFE8: SH3_R(1, basrb_);
    case 0xFFFFFFEC: SH3_R(4, ccr_);
    case 0xFFFFFFF0: SH3_R(4, pteh_);
    case 0xFFFFFFF4: SH3_R(4, ptel_);
    case 0xFFFFFFF8: SH3_R(4, ttb_);
    case 0xFFFFFFFC: SH3_R(4, tea_);
    default: return false;
  }
}

bool Peripherals::p4_write(u32 a, unsigned size, u32 v) {
  switch (a) {
    // SCI
    case 0xFFFFFE80: SH3_W(1, sci_.smr = u8(v); sci_update_levels());
    case 0xFFFFFE82: SH3_W(1, sci_.brr = u8(v));
    case 0xFFFFFE84:
      SH3_W(1, {
        sci_.scr = u8(v);
        if (!(sci_.scr & kScrRe)) { cancel(sci_.rx_event); sci_.rx_active = false; }
        sci_try_start_rx(now());
        sci_update_levels();
      });
    case 0xFFFFFE86:
      SH3_W(1, {
        sci_.tdr = u8(v);
        if ((sci_.scr & kScrTe) && sci_.tx_hook) sci_.tx_hook(sci_.tdr);
      });
    case 0xFFFFFE88:
      // Flags clear by writing 0 after reading 1; TDRE / TEND stay set.
      SH3_W(1, {
        sci_.ssr = u8((sci_.ssr & (u8(v) | kSsrTdre | kSsrTend | 0x03)) | kSsrTdre | kSsrTend);
        sci_update_levels();
      });
    case 0xFFFFFE8C: SH3_W(1, sci_.scmr = u8(v));
    // TMU
    case 0xFFFFFE90: SH3_W(1, tocr_ = u8(v & 1));
    case 0xFFFFFE92:
      SH3_W(1, {
        for (unsigned i = 0; i < 3; ++i) tmu_sync(i, now());
        tstr_ = u8(v & 7);
        for (unsigned i = 0; i < 3; ++i) { tmu_[i].synced = now(); tmu_reschedule(i); }
      });
    case 0xFFFFFE94: SH3_W(4, tmu_sync(0, now()); tmu_[0].tcor = v; tmu_reschedule(0));
    case 0xFFFFFE98: SH3_W(4, tmu_sync(0, now()); tmu_[0].tcnt = v; tmu_reschedule(0));
    case 0xFFFFFEA0: SH3_W(4, tmu_sync(1, now()); tmu_[1].tcor = v; tmu_reschedule(1));
    case 0xFFFFFEA4: SH3_W(4, tmu_sync(1, now()); tmu_[1].tcnt = v; tmu_reschedule(1));
    case 0xFFFFFEAC: SH3_W(4, tmu_sync(2, now()); tmu_[2].tcor = v; tmu_reschedule(2));
    case 0xFFFFFEB0: SH3_W(4, tmu_sync(2, now()); tmu_[2].tcnt = v; tmu_reschedule(2));
    case 0xFFFFFE9C: case 0xFFFFFEA8: case 0xFFFFFEB4: {
      if (size != 2) return false;
      const unsigned ch = (a - 0xFFFFFE9C) / 12;
      Timer& tm = tmu_[ch];
      tmu_sync(ch, now());
      // UNF / ICPF clear by writing 0 after reading 1; a prescaler change restarts the period.
      const u16 flags = u16(tm.tcr & u16(v) & 0x300);
      if ((v & 7) != (tm.tcr & 7)) tm.presc = 0;
      tm.tcr = u16((v & 0xFF) | flags);
      tmu_reschedule(ch);
      tmu_update_level(ch);
      return true;
    }
    case 0xFFFFFEB8: SH3_W(4, tcpr2_ = v);
    // RTC
    case 0xFFFFFEC2: SH3_W(1, rtc_sync(); rtc_[1] = u8(v));
    case 0xFFFFFEC4: SH3_W(1, rtc_sync(); rtc_[2] = u8(v));
    case 0xFFFFFEC6: SH3_W(1, rtc_sync(); rtc_[3] = u8(v));
    case 0xFFFFFEC8: SH3_W(1, rtc_sync(); rtc_[4] = u8(v));
    case 0xFFFFFECA: SH3_W(1, rtc_sync(); rtc_[5] = u8(v));
    case 0xFFFFFECC: SH3_W(1, rtc_sync(); rtc_[6] = u8(v));
    case 0xFFFFFECE: SH3_W(1, rtc_sync(); rtc_[7] = u8(v));
    case 0xFFFFFED0: SH3_W(1, rtc_[8] = u8(v));
    case 0xFFFFFED2: SH3_W(1, rtc_[9] = u8(v));
    case 0xFFFFFED4: SH3_W(1, rtc_[10] = u8(v));
    case 0xFFFFFED6: SH3_W(1, rtc_[11] = u8(v));
    case 0xFFFFFED8: SH3_W(1, rtc_[12] = u8(v));
    case 0xFFFFFEDA: SH3_W(1, rtc_[13] = u8(v));
    case 0xFFFFFEDC: SH3_W(1, rcr1_ = u8(v & 0x99));
    case 0xFFFFFEDE:
      SH3_W(1, {
        rtc_sync();
        rcr2_ = u8(v & 0x7D);  // RESET (bit 1) self-clears
        if (v & 0x02) { rtc_[0] = 0; rtc_frac_ = 0; }
      });
    // INTC
    case 0xFFFFFEE0:
      SH3_W(2, {
        icr0_ = u16(v & 0x0100);  // NMIE; NMIL is the pin
        update_irq();
      });
    case 0xFFFFFEE2: SH3_W(2, ipr_[0] = u16(v); recompute_priorities());
    case 0xFFFFFEE4: SH3_W(2, ipr_[1] = u16(v); recompute_priorities());
    // BSC
    case 0xFFFFFF60: SH3_W(2, bcr1_ = u16(v));
    case 0xFFFFFF62: SH3_W(2, bcr2_ = u16(v));
    case 0xFFFFFF64: SH3_W(2, wcr1_ = u16(v));
    case 0xFFFFFF66: SH3_W(2, wcr2_ = u16(v));
    case 0xFFFFFF68: SH3_W(2, mcr_ = u16(v));
    case 0xFFFFFF6A: SH3_W(2, dcr_ = u16(v));
    case 0xFFFFFF6C: SH3_W(2, bsc_pcr_ = u16(v));
    // The refresh registers take H'A5 in the upper byte as a write password.
    case 0xFFFFFF6E:
      SH3_W(2, {
        if ((v & 0xFF00) != 0xA500) return true;
        refresh_sync();
        // CMF / OVF clear by writing 0 after reading 1.
        rtcsr_ = u16((v & 0x7B) | (rtcsr_ & u16(v) & 0x84));
        set_level(kSrcRcmi, (rtcsr_ & 0x80) && (rtcsr_ & 0x40));
        set_level(kSrcRovi, (rtcsr_ & 0x04) && (rtcsr_ & 0x02));
        refresh_reschedule();
      });
    case 0xFFFFFF70: SH3_W(2, if ((v & 0xFF00) == 0xA500) { refresh_sync(); rtcnt_ = u16(v & 0xFF); refresh_reschedule(); });
    case 0xFFFFFF72: SH3_W(2, if ((v & 0xFF00) == 0xA500) { refresh_sync(); rtcor_ = u16(v & 0xFF); refresh_reschedule(); });
    case 0xFFFFFF74: SH3_W(2, if ((v & 0xFF00) == 0xA500) { refresh_sync(); rfcr_ = u16(v & 0x3FF); });
    case 0xFFFFFF7E: SH3_W(2, bcr3_ = u16(v));
    // CPG / WDT
    case 0xFFFFFF80:
      SH3_W(2, {
        const u32 old_p = pclk_div_;
        for (unsigned i = 0; i < 3; ++i) tmu_sync(i, now());
        wdt_sync(now());
        refresh_sync();
        cmt_sync();
        apply_frqcr(u16(v));
        if (pclk_div_ != old_p) {
          for (unsigned i = 0; i < 3; ++i) { tmu_[i].presc = 0; tmu_reschedule(i); }
          wdt_presc_ = 0;
          wdt_reschedule();
          cmt_presc_ = 0;
          refresh_presc_ = 0;
          refresh_reschedule();
        }
      });
    case 0xFFFFFF82: SH3_W(1, stbcr_ = u8(v));
    case 0xFFFFFF84:  // password in the high byte
      SH3_W(2, {
        if ((v & 0xFF00) == 0x5A00) { wdt_sync(now()); wtcnt_ = u8(v); wdt_presc_ = 0; wdt_reschedule(); }
      });
    case 0xFFFFFF86:
      SH3_W(2, {
        if ((v & 0xFF00) == 0xA500) {
          wdt_sync(now());
          const u8 old = wtcsr_;
          // WOVF / IOVF clear by writing 0.
          wtcsr_ = u8((v & 0xE7) | (old & u8(v) & 0x18));
          set_level(kSrcIti, (wtcsr_ & 0x08) != 0);
          if ((wtcsr_ ^ old) & 0x87) wdt_presc_ = 0;
          wdt_reschedule();
        }
      });
    case 0xFFFFFF88: SH3_W(1, stbcr2_ = u8(v));
    // UBC (stored)
    case 0xFFFFFF90: SH3_W(4, ubc_[0] = v);
    case 0xFFFFFF94: SH3_W(4, ubc_[1] = v);
    case 0xFFFFFF98: SH3_W(2, ubc_[2] = v);
    case 0xFFFFFF9C: SH3_W(2, ubc_[3] = v);
    case 0xFFFFFFA0: SH3_W(4, ubc_[4] = v);
    case 0xFFFFFFA4: SH3_W(1, ubc_[5] = v);
    case 0xFFFFFFA8: SH3_W(2, ubc_[6] = v);
    case 0xFFFFFFAC: SH3_W(4, ubc_[7] = v);
    case 0xFFFFFFB0: SH3_W(4, ubc_[8] = v);
    case 0xFFFFFFB4: SH3_W(1, ubc_[9] = v);
    case 0xFFFFFFB8: SH3_W(2, ubc_[10] = v);
    case 0xFFFFFFBC: SH3_W(4, ubc_[11] = v);
    // CCN
    case 0xFFFFFFD0: SH3_W(4, cpu_.set_tra(v));
    case 0xFFFFFFD4: SH3_W(4, cpu_.set_expevt(v));
    case 0xFFFFFFD8: SH3_W(4, cpu_.set_intevt(v));
    case 0xFFFFFFE0:
      SH3_W(4, {
        mmucr_ = v & ~0x4u;  // TF self-clears; AT has no effect: address translation is not modelled
      });
    case 0xFFFFFFE4: SH3_W(1, basra_ = u8(v));
    case 0xFFFFFFE8: SH3_W(1, basrb_ = u8(v));
    case 0xFFFFFFEC: SH3_W(4, ccr_ = v & ~0x8u);  // CF self-clears
    case 0xFFFFFFF0: SH3_W(4, pteh_ = v);
    case 0xFFFFFFF4: SH3_W(4, ptel_ = v);
    case 0xFFFFFFF8: SH3_W(4, ttb_ = v);
    case 0xFFFFFFFC: SH3_W(4, tea_ = v);
    default: return false;
  }
}

// ---------------------------------------------------------------------------
// Area-1 registers (INTC part 2, DMAC, CMT, A/D, D/A, CCR2, ports, IrDA, SCIF, UDI)

bool Peripherals::area1_read(u32 off, unsigned size, u32& v) {
  if (off >= 0x20 && off < 0x60) {
    if (size != 4) return false;
    const Dma& d = dma_[(off - 0x20) >> 4];
    switch (off & 0xC) {
      case 0x0: v = d.sar; break;
      case 0x4: v = d.dar; break;
      case 0x8: v = d.tcr; break;
      default: v = d.chcr; break;
    }
    return true;
  }
  if (off >= 0x80 && off < 0x90) {
    // ADDRxH / ADDRxL: 10-bit result left-justified over the pair.
    if (size != 1) return false;
    const u16 r = addr_[(off - 0x80) >> 2];
    v = (off & 2) ? (r & 0xC0) : (r >> 8);
    return true;
  }
  if (off >= 0x100 && off < 0x118) SH3_R(2, pcr_[(off - 0x100) >> 1]);
  if (off >= 0x120 && off < 0x138 && !(off & 1)) SH3_R(1, port_read((off - 0x120) >> 1));
  if (off >= 0x140 && off < 0x150) return scif_read(0, off - 0x140, size, v);
  if (off >= 0x150 && off < 0x160) return scif_read(1, off - 0x150, size, v);
  switch (off) {
    case 0x000: SH3_R(4, cpu_.intevt2());
    case 0x004: SH3_R(1, irr0_);
    case 0x006: SH3_R(1, irr1());
    case 0x008: SH3_R(1, irr2());
    case 0x010: SH3_R(2, icr1_);
    case 0x012: SH3_R(2, icr2_);
    case 0x014: SH3_R(2, pinter_);
    case 0x016: SH3_R(2, ipr_[2]);
    case 0x018: SH3_R(2, ipr_[3]);
    case 0x01A: SH3_R(2, ipr_[4]);
    case 0x060: SH3_R(2, dmaor_);
    case 0x070: SH3_R(2, cmstr_);
    case 0x072: cmt_sync(); SH3_R(2, cmcsr_);
    case 0x074: cmt_sync(); SH3_R(2, cmcnt_);
    case 0x076: SH3_R(2, cmcor_);
    case 0x090: SH3_R(1, adcsr_);
    case 0x092: SH3_R(1, adcr_);
    case 0x0A0: SH3_R(1, dadr_[0]);
    case 0x0A2: SH3_R(1, dadr_[1]);
    case 0x0A4: SH3_R(1, dacr_);
    case 0x0B0: SH3_R(4, ccr2_);
    case 0x200: SH3_R(2, sdir_);
    default: return false;
  }
}

bool Peripherals::area1_write(u32 off, unsigned size, u32 v) {
  if (off >= 0x20 && off < 0x60) {
    if (size != 4) return false;
    const unsigned ch = (off - 0x20) >> 4;
    Dma& d = dma_[ch];
    switch (off & 0xC) {
      case 0x0: d.sar = v; break;
      case 0x4: d.dar = v; break;
      case 0x8: d.tcr = v & 0x00FFFFFF; break;
      default: d.chcr = v; dmac_check(ch); break;
    }
    return true;
  }
  if (off >= 0x80 && off < 0x90) return size == 1;  // result registers: writes ignored
  if (off >= 0x100 && off < 0x118) {
    if (size != 2) return false;
    const unsigned port = (off - 0x100) >> 1;
    pcr_[port] = u16(v);
    if (port_out_) port_out_(port, pdr_[port], pcr_[port]);
    return true;
  }
  if (off >= 0x120 && off < 0x138 && !(off & 1)) {
    if (size != 1) return false;
    const unsigned port = (off - 0x120) >> 1;
    pdr_[port] = u8(v);
    if (port_out_) port_out_(port, pdr_[port], pcr_[port]);
    return true;
  }
  if (off >= 0x140 && off < 0x150) return scif_write(0, off - 0x140, size, v);
  if (off >= 0x150 && off < 0x160) return scif_write(1, off - 0x150, size, v);
  switch (off) {
    case 0x000: SH3_W(4, cpu_.set_intevt2(v));
    case 0x004:
      // IRR0: an edge-latched IRQ request is withdrawn by writing 0 to its
      // flag; level-sensed flags and the PINT flags follow the pins.
      SH3_W(1, {
        for (unsigned pin = 0; pin < 6; ++pin) {
          const u8 bit = u8(1u << pin);
          if ((irr0_ & bit) && !(v & bit) && irq_sense(pin) < 2) {
            irr0_ &= u8(~bit);
            withdraw(kSrcIrq0 + pin);
          }
        }
      });
    case 0x010:
      SH3_W(2, {
        icr1_ = u16(v);
        resample_irq_pins();
        recompute_priorities();
      });
    case 0x012: SH3_W(2, icr2_ = u16(v); update_pint());
    case 0x014: SH3_W(2, pinter_ = u16(v); update_pint());
    case 0x016: SH3_W(2, ipr_[2] = u16(v); recompute_priorities());
    case 0x018: SH3_W(2, ipr_[3] = u16(v); recompute_priorities());
    case 0x01A: SH3_W(2, ipr_[4] = u16(v); recompute_priorities());
    case 0x060:
      SH3_W(2, {
        const u16 old = dmaor_;
        dmaor_ = u16(v);
        // AE / NMIF can only be cleared.
        if ((dmaor_ & kDmaorAE) && !(old & kDmaorAE)) dmaor_ &= u16(~kDmaorAE);
        if ((dmaor_ & kDmaorNMIF) && !(old & kDmaorNMIF)) dmaor_ &= u16(~kDmaorNMIF);
        for (unsigned i = 0; i < 4; ++i) dmac_check(i);
      });
    case 0x070: SH3_W(2, cmt_sync(); cmstr_ = u16(v & 1); cmt_synced_ = now());
    case 0x072:
      SH3_W(2, {
        cmt_sync();
        cmcsr_ = u16((v & 0x43) | (cmcsr_ & u16(v) & 0x80));  // CMF clears by writing 0
        cmt_presc_ = 0;
      });
    case 0x074: SH3_W(2, cmt_sync(); cmcnt_ = u16(v));
    case 0x076: SH3_W(2, cmt_sync(); cmcor_ = u16(v));
    case 0x090:
      SH3_W(1, {
        const u8 old = adcsr_;
        adcsr_ = u8((v & 0x7F) | (old & u8(v) & 0x80));  // ADF clears by writing 0
        if ((adcsr_ & 0x20) && !(old & 0x20)) adc_start();
        if (!(adcsr_ & 0x20)) cancel(adc_event_);
        set_level(kSrcAdi, (adcsr_ & 0x80) && (adcsr_ & 0x40));
      });
    case 0x092: SH3_W(1, adcr_ = u8(v));
    case 0x0A0: SH3_W(1, dadr_[0] = u8(v));
    case 0x0A2: SH3_W(1, dadr_[1] = u8(v));
    case 0x0A4: SH3_W(1, dacr_ = u8(v));
    case 0x0B0: SH3_W(4, ccr2_ = v);
    case 0x200: SH3_W(2, sdir_ = u16(v));
    default: return false;
  }
}

// ---------------------------------------------------------------------------
// Interrupt controller (section 6)

void Peripherals::recompute_priorities() {
  priority_.fill(0);
  priority_[kSrcNmi] = kNmiPriority;
  const u16 ipra = ipr_[0], iprb = ipr_[1], iprc = ipr_[2], iprd = ipr_[3], ipre = ipr_[4];
  priority_[kSrcIrq0] = u8(iprc & 15);
  priority_[kSrcIrq1] = u8((iprc >> 4) & 15);
  priority_[kSrcIrq2] = u8((iprc >> 8) & 15);
  priority_[kSrcIrq3] = u8(iprc >> 12);
  priority_[kSrcIrq4] = u8(iprd & 15);
  priority_[kSrcIrq5] = u8((iprd >> 4) & 15);
  priority_[kSrcPint07] = u8(iprd >> 12);
  priority_[kSrcPint815] = u8((iprd >> 8) & 15);
  priority_[kSrcIrlLevel] = u8(15 - irl_level_);
  for (unsigned s = kSrcDei0; s <= kSrcDei3; ++s) priority_[s] = u8(ipre >> 12);
  for (unsigned s = kSrcIrdaEri; s <= kSrcIrdaTxi; ++s) priority_[s] = u8((ipre >> 8) & 15);
  for (unsigned s = kSrcScifEri; s <= kSrcScifTxi; ++s) priority_[s] = u8((ipre >> 4) & 15);
  priority_[kSrcAdi] = u8(ipre & 15);
  priority_[kSrcTuni0] = u8(ipra >> 12);
  priority_[kSrcTuni1] = u8((ipra >> 8) & 15);
  priority_[kSrcTuni2] = priority_[kSrcTicpi2] = u8((ipra >> 4) & 15);
  priority_[kSrcAti] = priority_[kSrcPri] = priority_[kSrcCui] = u8(ipra & 15);
  priority_[kSrcIti] = u8(iprb >> 12);
  priority_[kSrcRcmi] = priority_[kSrcRovi] = u8((iprb >> 8) & 15);
  for (unsigned s = kSrcSciEri; s <= kSrcSciTei; ++s) priority_[s] = u8((iprb >> 4) & 15);
  update_irq();
}

int Peripherals::pending_priority(u32& intevt, u32& intevt2, unsigned& source) const {
  if (!requesting_) return -1;
  int best = -1;
  for (unsigned s = 0; s < kSourceCount; ++s) {
    if (!((requesting_ >> s) & 1)) continue;
    const int p = priority_[s];
    if (p <= best) continue;
    best = p;
    source = s;
    if (s == kSrcIrlLevel) {
      intevt = intevt2 = 0x200u + irl_level_ * 0x20u;
    } else {
      intevt2 = kIntevt2[s];
      // INTEVT keeps the SH7708 codes: the sources added with INTEVT2 report
      // the code of their priority level there.
      intevt = intevt2 >= 0x600 ? 0x3E0u - u32(p) * 0x20u : intevt2;
    }
  }
  return best;
}

void Peripherals::acknowledge(unsigned source) {
  // The NMI is edge-latched and drops on acceptance; module levels stay
  // until the handler clears the flag, and an edge-detected IRQ pin until
  // the handler clears its IRR0 flag.
  if (source == kSrcNmi) withdraw(source);
}

void Peripherals::request(unsigned source) {
  const u64 bit = u64(1) << source;
  if (requesting_ & bit) return;
  requesting_ |= bit;
  update_irq();
}

void Peripherals::withdraw(unsigned source) {
  const u64 bit = u64(1) << source;
  if (!(requesting_ & bit)) return;
  requesting_ &= ~bit;
  update_irq();
}

void Peripherals::set_irq_pin(unsigned pin, bool high) {
  if (pin > 5) return;
  const bool was = irq_high_[pin];
  irq_high_[pin] = high;
  if (irl_mode()) return;  // the pins carry the encoded level: see set_irl_level()
  const u8 bit = u8(1u << pin);
  switch (irq_sense(pin)) {
    case 0:  // falling edge
      if (was && !high) { irr0_ |= bit; request(kSrcIrq0 + pin); }
      break;
    case 1:  // rising edge
      if (!was && high) { irr0_ |= bit; request(kSrcIrq0 + pin); }
      break;
    default:  // low level
      if (!high) { irr0_ |= bit; request(kSrcIrq0 + pin); }
      else { irr0_ &= u8(~bit); withdraw(kSrcIrq0 + pin); }
      break;
  }
}

void Peripherals::set_pint_pin(unsigned pin, bool high) {
  if (pin > 15) return;
  const u16 bit = u16(1u << pin);
  pint_high_ = high ? u16(pint_high_ | bit) : u16(pint_high_ & ~bit);
  update_pint();
}

// A pin requests while PINTER enables it and its level matches its ICR2 bit;
// IRR0 bit 7 (PINT0-7) and bit 6 (PINT8-15) show the two group requests.
void Peripherals::update_pint() {
  const u16 active = u16(~(pint_high_ ^ icr2_) & pinter_);
  const bool low = (active & 0x00FF) != 0, high = (active & 0xFF00) != 0;
  irr0_ = u8((irr0_ & 0x3F) | (low ? 0x80 : 0) | (high ? 0x40 : 0));
  set_level(kSrcPint07, low);
  set_level(kSrcPint815, high);
}

// ICR1 changed: level-sensed pins follow their level, edge latches keep, and
// the IRQ / IRL interpretation of the pins switches.
void Peripherals::resample_irq_pins() {
  for (unsigned pin = 0; pin < 6; ++pin) {
    const u8 bit = u8(1u << pin);
    if (irl_mode()) {
      irr0_ &= u8(~bit);
      withdraw(kSrcIrq0 + pin);
    } else if (irq_sense(pin) >= 2) {
      if (!irq_high_[pin]) { irr0_ |= bit; request(kSrcIrq0 + pin); }
      else { irr0_ &= u8(~bit); withdraw(kSrcIrq0 + pin); }
    }
  }
  set_level(kSrcIrlLevel, irl_mode() && irl_level_ < 15);
}

void Peripherals::set_irl_level(unsigned level) {
  irl_level_ = level & 15;
  priority_[kSrcIrlLevel] = u8(15 - irl_level_);
  set_level(kSrcIrlLevel, irl_mode() && irl_level_ < 15);
  update_irq();
}

void Peripherals::set_nmi_pin(bool high) {
  if (nmi_high_ == high) return;
  const bool rising = high && !nmi_high_;
  nmi_high_ = high;
  const bool edge = (icr0_ & 0x100) ? rising : !rising;
  if (edge) request(kSrcNmi);
}

u8 Peripherals::irr1() const {
  u8 v = 0;
  for (unsigned i = 0; i < 4; ++i) if ((requesting_ >> (kSrcDei0 + i)) & 1) v |= u8(0x10u << i);
  if ((requesting_ >> kSrcIrdaTxi) & 1) v |= 0x08;
  if ((requesting_ >> kSrcIrdaBri) & 1) v |= 0x04;
  if ((requesting_ >> kSrcIrdaRxi) & 1) v |= 0x02;
  if ((requesting_ >> kSrcIrdaEri) & 1) v |= 0x01;
  return v;
}

u8 Peripherals::irr2() const {
  u8 v = 0;
  if ((requesting_ >> kSrcAdi) & 1) v |= 0x20;
  if ((requesting_ >> kSrcScifTxi) & 1) v |= 0x08;
  if ((requesting_ >> kSrcScifBri) & 1) v |= 0x04;
  if ((requesting_ >> kSrcScifRxi) & 1) v |= 0x02;
  if ((requesting_ >> kSrcScifEri) & 1) v |= 0x01;
  return v;
}

// ---------------------------------------------------------------------------
// Bus state controller: the refresh counter (section 9).  RTCNT counts the
// bus clock divided per RTCSR.CKS; a match with RTCOR sets CMF, counts a
// refresh in RFCR and requests RCMI when CMIE is set.

u32 Peripherals::refresh_divider() const {
  static constexpr u32 div[8] = {0, 4, 16, 64, 256, 1024, 2048, 4096};
  return div[(rtcsr_ >> 3) & 7];
}

void Peripherals::refresh_sync() {
  const u64 t = now();
  const u32 div = refresh_divider();
  if (!div) { refresh_synced_ = t; refresh_presc_ = 0; return; }
  const u64 states_per_count = u64(div) * bclk_div_;
  const u64 total = refresh_presc_ + (t - refresh_synced_);
  u64 counts = total / states_per_count;
  refresh_presc_ = u32(total % states_per_count);
  refresh_synced_ = t;
  // The first match ends the period in progress; every later one is a whole
  // period of RTCOR counts (the counter clears as it reaches RTCOR; 256 for
  // an RTCOR of 0), so they are added up rather than replayed.
  const u32 to_match = ((u32(rtcor_ & 0xFF) - (rtcnt_ & 0xFF) - 1) & 0xFF) + 1;
  if (counts < to_match) {
    rtcnt_ = u16((rtcnt_ + counts) & 0xFF);
  } else {
    counts -= to_match;
    const u64 period = (rtcor_ & 0xFF) ? u32(rtcor_ & 0xFF) : 256u;
    const u64 matches = 1 + counts / period;
    rtcnt_ = u16(counts % period);
    rtcsr_ |= 0x80;  // CMF
    const u16 limit = (rtcsr_ & 1) ? 512 : 1024;
    const u64 refreshes = rfcr_ + matches;
    if (refreshes >= limit) rtcsr_ |= 0x04;  // OVF
    rfcr_ = u16(refreshes % limit);
  }
  set_level(kSrcRcmi, (rtcsr_ & 0x80) && (rtcsr_ & 0x40));
  set_level(kSrcRovi, (rtcsr_ & 0x04) && (rtcsr_ & 0x02));
}

// A match is only worth an event while an interrupt can come of it; the
// counter and its flags are brought up to date when they are read.
void Peripherals::refresh_reschedule() {
  cancel(refresh_event_);
  const u32 div = refresh_divider();
  if (!div || !(rtcsr_ & 0x42)) return;
  const u32 to_match = (u32(rtcor_ & 0xFF) - (rtcnt_ & 0xFF) - 1) & 0xFF;
  const u64 when = refresh_synced_ - refresh_presc_ + u64(to_match + 1) * div * bclk_div_;
  refresh_event_ = sched_.schedule(when, &on_refresh, this);
}

void Peripherals::on_refresh(void* self, u64, u64) {
  auto* p = static_cast<Peripherals*>(self);
  p->refresh_event_ = 0;
  p->refresh_sync();
  p->refresh_reschedule();
}

// ---------------------------------------------------------------------------
// DMA controller (section 8)

// Auto-request channels transfer as soon as they are enabled; the
// external-request ones wait for DREQ.
void Peripherals::dmac_check(unsigned ch) {
  Dma& d = dma_[ch];
  if (!(d.chcr & kChcrDE) || !(dmaor_ & kDmaorDME)) {
    if (d.event) cancel(d.event);
    if (!(d.chcr & kChcrTE) || !(d.chcr & kChcrIE)) withdraw(kSrcDei0 + ch);
    return;
  }
  set_level(kSrcDei0 + ch, (d.chcr & kChcrTE) && (d.chcr & kChcrIE));
  const unsigned rs = (d.chcr >> 8) & 15;
  if (rs != 4) return;
  if (d.event || (d.chcr & kChcrTE) || (dmaor_ & (kDmaorAE | kDmaorNMIF))) return;
  dma_transfer(ch, true);
}

bool Peripherals::dma_ext_request(unsigned ch) {
  if (ch > 3) return false;
  Dma& d = dma_[ch];
  if (!(dmaor_ & kDmaorDME) || (dmaor_ & (kDmaorAE | kDmaorNMIF))) return false;
  if (d.event || !(d.chcr & kChcrDE) || (d.chcr & kChcrTE)) return false;
  const unsigned rs = (d.chcr >> 8) & 15;
  if (rs != 0 && rs != 2 && rs != 3) return false;
  dma_transfer(ch, false);
  return true;
}

void Peripherals::dma_transfer(unsigned ch, bool delayed_completion) {
  Dma& d = dma_[ch];
  const unsigned incd = (d.chcr >> 14) & 3, incs = (d.chcr >> 12) & 3;
  // SH7706/SH7709A CHCR.TS is bits 4:3 (unlike the SH-4 layout).
  const u32 size = kDmaSize[(d.chcr >> 3) & 3];
  if (incd == 3 || incs == 3) return;  // reserved address modes: no transfer
  u32 src = d.sar & Bus::kPhysMask, dst = d.dar & Bus::kPhysMask;
  u32 count = d.tcr ? d.tcr : 0x1000000u;
  const u32 total = count;
  src &= ~(size - 1);
  dst &= ~(size - 1);
  for (; count > 0; --count) {
    if (incs == 2) src -= size;
    if (incd == 2) dst -= size;
    switch (size) {
      case 1: bus_.write8(dst & Bus::kPhysMask, bus_.read8(src & Bus::kPhysMask)); break;
      case 2: bus_.write16(dst & Bus::kPhysMask, bus_.read16(src & Bus::kPhysMask)); break;
      case 4: bus_.write32(dst & Bus::kPhysMask, bus_.read32(src & Bus::kPhysMask)); break;
      default:
        for (u32 i = 0; i < size; i += 4) bus_.write32((dst + i) & Bus::kPhysMask, bus_.read32((src + i) & Bus::kPhysMask));
        break;
    }
    if (incs == 1) src += size;
    if (incd == 1) dst += size;
  }
  d.sar = (d.sar & ~Bus::kPhysMask) | (src & Bus::kPhysMask);
  d.dar = (d.dar & ~Bus::kPhysMask) | (dst & Bus::kPhysMask);
  d.tcr = 0;
  if (delayed_completion) {
    // The bus is busy for the transfer: completion comes after it.
    d.event = sched_.schedule(now() + 2ull * total + 1, &on_dma, &d);
  } else {
    dma_complete(ch);
  }
}

void Peripherals::dma_complete(unsigned ch) {
  Dma& d = dma_[ch];
  d.event = 0;
  d.chcr |= kChcrTE;
  if (d.chcr & kChcrIE) request(kSrcDei0 + ch);
}

void Peripherals::on_dma(void* dma, u64, u64) {
  auto* d = static_cast<Dma*>(dma);
  d->owner->dma_complete(d->index);
}

// ---------------------------------------------------------------------------
// Watchdog (section 10.4): an 8-bit up counter on the peripheral clock
// divided per WTCSR.CKS.  Interval mode overflows request ITI; watchdog mode
// overflows reset the chip.

u32 Peripherals::wdt_states_per_count() const {
  static constexpr u32 div[8] = {2, 64, 128, 256, 512, 1024, 4096, 8192};
  return pclk_div_ * div[wtcsr_ & 7];
}

void Peripherals::wdt_sync(u64 t) {
  if (t < wdt_synced_) wdt_synced_ = t;
  if (!(wtcsr_ & 0x80)) { wdt_synced_ = t; return; }
  const u32 spc = wdt_states_per_count();
  const u64 total = wdt_presc_ + (t - wdt_synced_);
  u64 counts = total / spc;
  wdt_presc_ = u32(total % spc);
  wdt_synced_ = t;
  const u64 to_overflow = 256 - wtcnt_;
  if (counts < to_overflow) {
    wtcnt_ = u8(wtcnt_ + counts);
    return;
  }
  if (wtcsr_ & 0x40) {
    // watchdog mode: the first overflow is a manual reset
    wtcnt_ = 0;
    wtcsr_ |= 0x10;  // WOVF
    cpu_.manual_reset();
    return;
  }
  // interval mode: however many overflows went by, one request stands
  wtcnt_ = u8((counts - to_overflow) & 0xFF);
  wtcsr_ |= 0x08;  // IOVF
  request(kSrcIti);
}

void Peripherals::wdt_reschedule() {
  cancel(wdt_event_);
  if (!(wtcsr_ & 0x80)) return;
  const u64 when = wdt_synced_ - wdt_presc_ + u64(256 - wtcnt_) * wdt_states_per_count();
  wdt_event_ = sched_.schedule(when, &on_wdt, this);
}

void Peripherals::on_wdt(void* self, u64, u64) {
  auto* p = static_cast<Peripherals*>(self);
  p->wdt_event_ = 0;
  p->wdt_sync(p->now());
  p->wdt_reschedule();
}

u8 Peripherals::wdt_count() {
  wdt_sync(now());
  return wtcnt_;
}

// ---------------------------------------------------------------------------
// Realtime clock (section 11): counts from the emulated clock while RCR2.START.

namespace {
u8 to_bcd(unsigned v) { return u8(((v / 10) << 4) | (v % 10)); }
unsigned from_bcd(u8 v) { return (v >> 4) * 10 + (v & 15); }
constexpr u8 kDaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
}  // namespace

void Peripherals::rtc_sync() {
  const u64 t = now();
  if (!(rcr2_ & 1)) { rtc_synced_ = t; return; }
  const u64 total = rtc_frac_ + (t - rtc_synced_);
  rtc_synced_ = t;
  u64 ticks = total * 128 / cpu_clock_hz_;  // 128 Hz: R64CNT bit 0 toggles at 64 Hz
  rtc_frac_ = total - ticks * cpu_clock_hz_ / 128;
  while (ticks--) {
    rtc_[0] = u8((rtc_[0] + 1) & 0x7F);
    if (rtc_[0] & 0x7F) continue;
    unsigned sec = from_bcd(rtc_[1]) + 1;
    if (sec < 60) { rtc_[1] = to_bcd(sec); continue; }
    rtc_[1] = 0;
    unsigned min = from_bcd(rtc_[2]) + 1;
    if (min < 60) { rtc_[2] = to_bcd(min); continue; }
    rtc_[2] = 0;
    unsigned hour = from_bcd(rtc_[3]) + 1;
    if (hour < 24) { rtc_[3] = to_bcd(hour); continue; }
    rtc_[3] = 0;
    rtc_[4] = u8((rtc_[4] + 1) % 7);
    const unsigned month = std::clamp(from_bcd(rtc_[6]), 1u, 12u);
    const unsigned year = from_bcd(rtc_[7]);
    unsigned days = kDaysInMonth[month - 1] + (month == 2 && year % 4 == 0 ? 1 : 0);
    unsigned day = from_bcd(rtc_[5]) + 1;
    if (day <= days) { rtc_[5] = to_bcd(day); continue; }
    rtc_[5] = 1;
    if (month < 12) { rtc_[6] = to_bcd(month + 1); continue; }
    rtc_[6] = 1;
    rtc_[7] = to_bcd((year + 1) % 100);
  }
}

// ---------------------------------------------------------------------------
// Timer unit (section 12): three 32-bit down counters on the peripheral
// clock divided by 4 to 256 (1024 on the reserved code, as the SH7750).
// Underflow reloads TCOR and sets UNF.

u32 Peripherals::tmu_states_per_count(unsigned ch) const { return pclk_div_ * kTmuDiv[tmu_[ch].tcr & 7]; }

void Peripherals::tmu_sync(unsigned ch, u64 t) {
  Timer& tm = tmu_[ch];
  if (t < tm.synced) tm.synced = t;
  if (!tmu_running(ch)) { tm.synced = t; return; }
  const u32 spc = tmu_states_per_count(ch);
  const u64 total = tm.presc + (t - tm.synced);
  u64 counts = total / spc;
  tm.presc = u32(total % spc);
  tm.synced = t;
  const u64 to_underflow = u64(tm.tcnt) + 1;  // counts until TCNT passes zero
  if (counts < to_underflow) {
    tm.tcnt -= u32(counts);
    return;
  }
  // The underflow in progress, then whole periods of TCOR + 1 counts: a
  // firmware that lets a short timer free-run gets them added up, not
  // replayed one by one.
  counts -= to_underflow;
  const u64 period = u64(tm.tcor) + 1;
  tm.tcnt = tm.tcor - u32(counts % period);
  tm.tcr |= 0x100;
  tmu_update_level(ch);
}

// An underflow is only worth an event while UNIE can turn it into an
// interrupt; the counter and UNF are brought up to date when they are read.
void Peripherals::tmu_reschedule(unsigned ch) {
  Timer& tm = tmu_[ch];
  cancel(tm.event);
  if (!tmu_running(ch) || !(tm.tcr & 0x20)) return;
  const u32 spc = tmu_states_per_count(ch);
  const u64 when = tm.synced - tm.presc + (u64(tm.tcnt) + 1) * spc;
  tm.event = sched_.schedule(when, &on_tmu, &tm);
}

void Peripherals::on_tmu(void* timer, u64, u64) {
  auto* tm = static_cast<Timer*>(timer);
  tm->event = 0;
  tm->owner->tmu_sync(tm->index, tm->owner->now());
  tm->owner->tmu_reschedule(tm->index);
}

void Peripherals::tmu_update_level(unsigned ch) {
  const Timer& tm = tmu_[ch];
  set_level(kSrcTuni0 + ch, (tm.tcr & 0x100) && (tm.tcr & 0x20));
}

u32 Peripherals::tmu_count(unsigned ch) {
  if (ch > 2) return 0;
  tmu_sync(ch, now());
  return tmu_[ch].tcnt;
}

// ---------------------------------------------------------------------------
// Compare match timer: a 16-bit up counter on the peripheral clock / 4..64
// that sets CMF on reaching CMCOR and restarts.  Synchronised lazily on
// register access; it raises no interrupt in this model.

u32 Peripherals::cmt_states_per_count() const {
  static constexpr u32 div[4] = {4, 8, 16, 64};
  return pclk_div_ * div[cmcsr_ & 3];
}

void Peripherals::cmt_sync() {
  const u64 t = now();
  if (!(cmstr_ & 1)) { cmt_synced_ = t; return; }
  const u32 spc = cmt_states_per_count();
  const u64 total = cmt_presc_ + (t - cmt_synced_);
  u64 counts = total / spc;
  cmt_presc_ = u32(total % spc);
  cmt_synced_ = t;
  const u64 to_match = u64(cmcor_) - cmcnt_ + 1;
  if (counts < to_match) {
    cmcnt_ = u16(cmcnt_ + counts);
    return;
  }
  cmcnt_ = u16((counts - to_match) % (u64(cmcor_) + 1));
  cmcsr_ |= 0x80;
}

// ---------------------------------------------------------------------------
// A/D converter (section 17): a conversion takes 536 peripheral clocks
// (266 with CKS set); single mode converts channel CH, multi mode the
// channels of its group up to CH.

void Peripherals::adc_start() {
  cancel(adc_event_);
  const u32 states = pclk_div_ * ((adcsr_ & 0x08) ? 266u : 536u);
  adc_event_ = sched_.schedule(now() + states, &on_adc, this);
}

void Peripherals::on_adc(void* self, u64, u64) {
  auto* p = static_cast<Peripherals*>(self);
  p->adc_event_ = 0;
  p->adc_complete();
}

void Peripherals::adc_complete() {
  const unsigned ch = adcsr_ & 7;
  const unsigned first = (adcsr_ & 0x10) ? (ch & 4) : ch;
  for (unsigned c = first; c <= ch; ++c) addr_[c & 3] = u16(adc_in_[c] << 6);
  adcsr_ = u8((adcsr_ | 0x80) & ~0x20);  // ADF set, ADST cleared
  set_level(kSrcAdi, (adcsr_ & 0x40) != 0);
}

// ---------------------------------------------------------------------------
// I/O ports (section 18): PxCR holds two bits per pin (00 other function,
// 01 output, 10 input with pull-up, 11 input); PxDR reads the pin for inputs
// and the latch for outputs.

u8 Peripherals::port_read(unsigned port) const {
  const u8 in = port_in_ ? port_in_(port) : u8(0xFF);
  u8 out_mask = 0;
  for (unsigned pin = 0; pin < 8; ++pin)
    if (((pcr_[port] >> (pin * 2)) & 3) == 1) out_mask |= u8(1u << pin);
  return u8((pdr_[port] & out_mask) | (in & ~out_mask));
}

// ---------------------------------------------------------------------------
// SCI (section 14): transmit data leaves through the hook as it is
// written; received bytes arrive one frame time apart and wait while RDRF is
// still set.

// One frame in CPU states.  The baud-rate generator divides the peripheral
// clock by 32 * 4^n * (N + 1) per bit in asynchronous mode (14.2.8: 64 *
// 2^(2n-1) * (N + 1)) and by 4 * 4^n * (N + 1) in clocked synchronous mode;
// a frame is start + 7/8 data + parity + 1/2 stop bits, or 8 bits clocked.
// The SCIF's generator and SMR bits are the same (15.2.9).
u32 Peripherals::frame_states(const Serial& s) const {
  if (s.byte_states) return s.byte_states;
  const unsigned n = s.smr & 3;
  const bool sync = (s.smr & 0x80) != 0;
  const u64 bit = u64(pclk_div_) * (sync ? 4u : 32u) * (1u << (2 * n)) * (u32(s.brr) + 1);
  const unsigned bits = sync ? 8 : 1 + ((s.smr & 0x40) ? 7 : 8) + ((s.smr & 0x20) ? 1 : 0) + ((s.smr & 0x08) ? 2 : 1);
  return u32(std::min<u64>(bit * bits, 0xFFFFFFFFu));
}

void Peripherals::sci_update_levels() {
  const Serial& s = sci_;
  set_level(kSrcSciRxi, (s.ssr & kSsrRdrf) && (s.scr & kScrRie) && (s.scr & kScrRe));
  set_level(kSrcSciTxi, (s.scr & kScrTie) && (s.scr & kScrTe));
  set_level(kSrcSciEri, (s.ssr & kSsrErrors) && (s.scr & kScrRie));
  set_level(kSrcSciTei, (s.scr & kScrTeie) && (s.scr & kScrTe));
}

void Peripherals::sci_try_start_rx(u64 at) {
  Serial& s = sci_;
  if (s.rx_active || s.queue.empty() || !(s.scr & kScrRe)) return;
  s.rx_active = true;
  s.rx_event = sched_.schedule(at + frame_states(s), &on_sci_rx, this);
}

void Peripherals::on_sci_rx(void* self, u64 when, u64) {
  auto* p = static_cast<Peripherals*>(self);
  Serial& s = p->sci_;
  s.rx_event = 0;
  s.rx_active = false;
  if (s.ssr & kSsrRdrf) {
    // The previous byte is still unread: hold the new one until it is.
    s.rx_active = true;
    s.rx_event = p->sched_.schedule(when + p->frame_states(s), &on_sci_rx, p);
    return;
  }
  s.rdr = s.queue.front();
  s.queue.pop_front();
  s.ssr |= kSsrRdrf;
  p->sci_update_levels();
  p->sci_try_start_rx(when);
}

void Peripherals::sci_receive(const u8* data, size_t len) {
  for (size_t i = 0; i < len; ++i) sci_.queue.push_back(data[i]);
  sci_try_start_rx(now());
}

// ---------------------------------------------------------------------------
// SCIF (sections 15, 16): the IrDA port and the SCIF share the layout: a
// 16-byte receive FIFO; RDF follows its contents, TDFE and TEND stay set.

void Peripherals::scif_update_levels(unsigned ch) {
  const Serial& s = scif_[ch];
  const unsigned base = ch ? kSrcScifEri : kSrcIrdaEri;
  set_level(base + 1, (s.fsr & kFsrRdf) && (s.scr & kScrRie) && (s.scr & kScrRe));  // RXI
  set_level(base + 3, (s.scr & kScrTie) && (s.scr & kScrTe));                        // TXI
  set_level(base + 0, (s.fsr & (kFsrEr | kFsrFer | kFsrPer)) && (s.scr & kScrRie));  // ERI
  set_level(base + 2, (s.fsr & kFsrBrk) && (s.scr & kScrRie));                       // BRI
}

void Peripherals::scif_try_start_rx(unsigned ch, u64 at) {
  Serial& s = scif_[ch];
  if (s.rx_active || s.queue.empty() || !(s.scr & kScrRe)) return;
  s.rx_active = true;
  s.rx_event = sched_.schedule(at + frame_states(s), &on_scif_rx, &s);
}

void Peripherals::on_scif_rx(void* serial, u64 when, u64) {
  auto* s = static_cast<Serial*>(serial);
  Peripherals* p = s->owner;
  s->rx_event = 0;
  s->rx_active = false;
  if (s->fifo.size() >= 16) {
    s->rx_active = true;
    s->rx_event = p->sched_.schedule(when + p->frame_states(*s), &on_scif_rx, s);
    return;
  }
  s->fifo.push_back(s->queue.front());
  s->queue.pop_front();
  s->fsr |= kFsrRdf;  // trigger level 1
  p->scif_update_levels(s->index);
  p->scif_try_start_rx(s->index, when);
}

void Peripherals::scif_receive(unsigned ch, const u8* data, size_t len) {
  ch &= 1;
  for (size_t i = 0; i < len; ++i) scif_[ch].queue.push_back(data[i]);
  scif_try_start_rx(ch, now());
}

bool Peripherals::scif_read(unsigned ch, u32 off, unsigned size, u32& v) {
  Serial& s = scif_[ch];
  switch (off) {
    case 0x0: SH3_R(1, s.smr);
    case 0x2: SH3_R(1, s.brr);
    case 0x4: SH3_R(1, s.scr);
    case 0x6: SH3_R(1, s.tdr);
    case 0x8: SH3_R(2, s.fsr | kFsrTdfe | kFsrTend);
    case 0xA:
      if (size != 1) return false;
      if (!s.fifo.empty()) {
        s.rdr = s.fifo.front();
        s.fifo.pop_front();
      }
      v = s.rdr;
      return true;
    case 0xC: SH3_R(1, s.fcr);
    case 0xE: SH3_R(2, u32(s.fifo.size()));  // receive count; the transmit FIFO is always empty
    default: return false;
  }
}

bool Peripherals::scif_write(unsigned ch, u32 off, unsigned size, u32 v) {
  Serial& s = scif_[ch];
  switch (off) {
    case 0x0: SH3_W(1, s.smr = u8(v); scif_update_levels(ch));
    case 0x2: SH3_W(1, s.brr = u8(v));
    case 0x4:
      SH3_W(1, {
        s.scr = u8(v);
        if (!(s.scr & kScrRe)) { cancel(s.rx_event); s.rx_active = false; }
        scif_try_start_rx(ch, now());
        scif_update_levels(ch);
      });
    case 0x6:
      SH3_W(1, {
        s.tdr = u8(v);
        if ((s.scr & kScrTe) && s.tx_hook) s.tx_hook(s.tdr);
      });
    case 0x8:
      SH3_W(2, {
        // Flags clear by writing 0 after reading 1; data waiting keeps RDF.
        s.fsr &= u16(v | kFsrTdfe | kFsrTend);
        if (!s.fifo.empty()) s.fsr |= kFsrRdf;
        scif_update_levels(ch);
      });
    case 0xC:
      SH3_W(1, {
        s.fcr = u8(v);
        if (s.fcr & 0x02) { s.fifo.clear(); s.fsr &= u16(~(kFsrRdf | kFsrDr)); }  // RFRST
        scif_update_levels(ch);
      });
    case 0xE: SH3_W(2, (void)0);
    default: return false;
  }
}

}  // namespace sh3

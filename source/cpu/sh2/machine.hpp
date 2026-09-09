// Machine: bus + CPU + scheduler + on-chip peripherals for one SH7014-family
// device or the SH7042.  Same shape as the H8/500 Machine: peripherals attach
// to the register field through the IoMux and to the scheduler for timed
// events; run() advances time in slices bounded by the next event.
//
// Module set (SH7014 hardware manual): INTC (6), CAC control (7), BSC (8),
// DMAC (9), MTU (10), WDT (11), SCI x2 (12), A/D (13 high-speed on the
// SH7014, 14 mid-speed on the SH7016/17), CMT (15), PFC + I/O ports (16, 17),
// flash control registers (18, SH7017), SBYCR (21).  The SH7042 (SH7040
// manual) is the superset: four DMAC channels, five MTU channels, two
// mid-speed A/D units (ADDRn at H'FFFF8400 / 8408, ADCSR at 8410 / 8411,
// ADCR at 8412 / 8413), ports A-F with the DTC and flash registers, and its
// own interrupt layout; it has no cache, so CCR is a plain register.
#pragma once
#include <algorithm>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/sh2/adc.hpp"
#include "cpu/sh2/bsc.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/chip.hpp"
#include "cpu/sh2/cmt.hpp"
#include "cpu/sh2/cpu.hpp"
#include "cpu/sh2/dmac.hpp"
#include "cpu/sh2/intc.hpp"
#include "cpu/sh2/mtu.hpp"
#include "cpu/sh2/ports.hpp"
#include "cpu/sh2/ports7042.hpp"
#include "cpu/sh2/sci.hpp"
#include "cpu/sh2/wdt.hpp"

namespace sh2 {

// Registers with no module of their own: SBYCR (H'FFFF8614) and the cache
// control register CCR (H'FFFF8740), both forwarded to the CPU.
class SysRegs final : public Device {
 public:
  static constexpr u32 kSbycr = 0xFFFF8614u, kCcr = 0xFFFF8740u;
  // has_cache: the SH7014/16/17 have the 1 KB instruction cache CCR controls;
  // the SH7042 has none, so its CCR bits are stored but drive nothing.
  SysRegs(Cpu& cpu, bool has_cache) : cpu_(cpu), has_cache_(has_cache) {}
  void map(emu::IoMux& mux) {
    mux.assign(kSbycr, 1, this);
    mux.assign(kCcr, 2, this);
  }
  void reset() {
    sbycr_ = 0x1F;
    ccr_stored_ = 0;
    cpu_.set_standby_request(false);
    cpu_.set_cache_control(0);
  }
  u8 read8(u32 a) override {
    if (a == kSbycr) return sbycr_;
    if (a == kCcr) return 0;
    if (a == kCcr + 1) return has_cache_ ? cpu_.cache_control() : ccr_stored_;
    return 0xFF;
  }
  void write8(u32 a, u8 v) override {
    if (a == kSbycr) {
      sbycr_ = u8((v & 0xC0) | 0x1F);
      cpu_.set_standby_request((v & 0x80) != 0);
    } else if (a == kCcr + 1) {
      if (has_cache_) cpu_.set_cache_control(v & 0x1F);
      else ccr_stored_ = v & 0x1F;
    }
  }
  u8 sbycr() const { return sbycr_; }

 private:
  Cpu& cpu_;
  bool has_cache_;
  u8 sbycr_ = 0x1F;
  u8 ccr_stored_ = 0;
};

class Machine final : public WdtResetSink {
 public:
  static constexpr u32 kAdc0 = 0xFFFF8400u;  // SH7042 A/D unit 0 ADDRA
  Machine(ChipModel model, u8 mode)
      : cfg_(make_chip_config(model, mode)),
        bus_(),
        cpu_(bus_, cfg_),
        io_(cfg_.regfield_base, cfg_.regfield_size),
        intc_(cpu_, intc_layout(model)),
        sysregs_(cpu_, model != ChipModel::SH7042),
        bsc_(sched_, cpu_, intc_, bus_, cfg_),
        dmac_(sched_, cpu_, intc_, bus_, cpu_, model == ChipModel::SH7042 ? 4 : 2),
        mtu_(sched_, cpu_, intc_, model == ChipModel::SH7042 ? 5 : 3),
        cmt_(sched_, cpu_, intc_),
        wdt_(sched_, cpu_, intc_),
        sci_{Sci(sched_, cpu_, intc_, Sci::kBase0, {IrqSrc::Eri0, IrqSrc::Rxi0, IrqSrc::Txi0, IrqSrc::Tei0}),
             Sci(sched_, cpu_, intc_, Sci::kBase1, {IrqSrc::Eri1, IrqSrc::Rxi1, IrqSrc::Txi1, IrqSrc::Tei1})},
        adc_hs_(sched_, cpu_, intc_),
        adc_ms_(sched_, cpu_, intc_, model == ChipModel::SH7042 ? kAdc0 : AdcMidSpeed::kBase,
                model == ChipModel::SH7042 ? kAdc0 + 0x10 : 0, model == ChipModel::SH7042 ? kAdc0 + 0x12 : 0),
        adc_ms1_(sched_, cpu_, intc_, kAdc0 + 8, kAdc0 + 0x11, kAdc0 + 0x13, IrqSrc::Adi1),
        ports_(cfg_),
        flash_(cfg_) {
    sched_.set_earlier_hook([](void* c, u64 when) { static_cast<Cpu*>(c)->cut_slice(when); }, &cpu_);
    // On-chip RAM (32-bit, 1 state) and, on the ROM parts in ROM-enabled
    // modes, the on-chip ROM at the bottom of the space.
    bus_.map_ram(cfg_.ram_base, cfg_.ram_size, Bus::kClsOnchipRam);
    if (cfg_.rom_enabled()) bus_.map_rom(0, cfg_.rom_size, Bus::kClsOnchipRom);
    // Register field: 16-bit 2-state by default; the SCI line is 8-bit, the
    // DMAC and cache lines 3-state (table 8.5).
    bus_.map_device(cfg_.regfield_base, cfg_.regfield_size, &io_, Bus::kClsPeriph16);
    bus_.map_device(0xFFFF8180u, 0x80, &io_, Bus::kClsPeriph8);
    bus_.map_device(0xFFFF8680u, 0x80, &io_, Bus::kClsPeriph16S3);
    bus_.map_device(0xFFFF8700u, 0x100, &io_, Bus::kClsPeriph16S3);
    bus_.set_noexec(cfg_.regfield_base, cfg_.regfield_size);

    intc_.map(io_);
    sysregs_.map(io_);
    bsc_.map(io_);
    dmac_.map(io_);
    mtu_.map(io_);
    cmt_.map(io_);
    wdt_.map(io_);
    sci_[0].map(io_);
    sci_[1].map(io_);
    if (model == ChipModel::SH7014) adc_hs_.map(io_); else adc_ms_.map(io_);
    if (model == ChipModel::SH7042) {
      adc_ms1_.map(io_);
      ports7042_.map(io_);
    } else {
      ports_.map(io_);
      flash_.map(io_);  // decodes nothing except on the SH7017
    }

    wdt_.set_reset_sink(this);
    // MTU TTGE output starts an A/D conversion.
    mtu_.set_adc_trigger([this](unsigned, u64) {
      if (cfg_.model == ChipModel::SH7014) adc_hs_.trigger(); else adc_ms_.trigger();
    });
    // DMAC transfers clear the requesting module's flag (9.3.2).
    dmac_.attach(IrqSrc::Tgi0a, [this] { mtu_.dmac_clear(IrqSrc::Tgi0a); });
    dmac_.attach(IrqSrc::Tgi1a, [this] { mtu_.dmac_clear(IrqSrc::Tgi1a); });
    dmac_.attach(IrqSrc::Tgi2a, [this] { mtu_.dmac_clear(IrqSrc::Tgi2a); });
    dmac_.attach(IrqSrc::Adi, [this] {
      if (cfg_.model == ChipModel::SH7014) adc_hs_.dmac_activated(); else adc_ms_.dmac_activated();
    });
    for (unsigned i = 0; i < 2; ++i) {
      Sci& s = sci_[i];
      const u32 tdr = (i ? Sci::kBase1 : Sci::kBase0) + 3;
      dmac_.attach(i ? IrqSrc::Rxi1 : IrqSrc::Rxi0, [&s] { s.dma_read_rdr(); });
      dmac_.attach(i ? IrqSrc::Txi1 : IrqSrc::Txi0, [&s, tdr] { s.dma_wrote_tdr(s.read8(tdr)); });
    }
  }

  Bus& bus() { return bus_; }
  Cpu& cpu() { return cpu_; }
  const Cpu& cpu() const { return cpu_; }
  emu::Scheduler& sched() { return sched_; }
  emu::IoMux& io() { return io_; }
  Intc& intc() { return intc_; }
  SysRegs& sysregs() { return sysregs_; }
  Bsc& bsc() { return bsc_; }
  Dmac& dmac() { return dmac_; }
  Mtu& mtu() { return mtu_; }
  Cmt& cmt() { return cmt_; }
  Wdt& wdt() { return wdt_; }
  Sci& sci(unsigned i) { return sci_[i]; }  // 0 = SCI0, 1 = SCI1
  // The A/D converter of this model (only one of the two is mapped).
  AdcHighSpeed& adc_high_speed() { return adc_hs_; }
  AdcMidSpeed& adc_mid_speed() { return adc_ms_; }
  AdcMidSpeed& adc_mid_speed1() { return adc_ms1_; }  // SH7042 unit 1
  Ports& ports() { return ports_; }
  Ports7042& ports7042() { return ports7042_; }
  FlashRegs& flash() { return flash_; }
  const ChipConfig& config() const { return cfg_; }

  u64 now() const { return cpu_.total_states(); }

  // NMI pin level; an active edge also stops the DMAC (9.3.6 NMIF).
  void set_nmi_pin(bool high) {
    if (intc_.set_nmi_pin(high)) dmac_.on_nmi();
  }

  // External reset (RES pin).
  void reset() { reset_internal(false); }
  // Internal reset from a watchdog overflow (11.2.3): RSTCSR keeps WOVF/RSTE,
  // ports and flash control keep their values.
  void watchdog_reset() override { reset_internal(true); }
  u64 resets() const { return resets_; }

  u64 run(u64 states) {
    const u64 start = now();
    const u64 end = start + states;
    while (now() < end) {
      sched_.run_due(now());
      cpu_.poll();
      const u64 next = std::min(sched_.next_time(), end);
      const u64 want = next > now() ? next - now() : 1;
      cpu_.run(want);
    }
    return now() - start;
  }

 private:
  void reset_internal(bool by_watchdog) {
    intc_.reset();
    sysregs_.reset();
    bsc_.reset();
    dmac_.reset();
    mtu_.reset();
    cmt_.reset();
    wdt_.reset(by_watchdog);
    sci_[0].reset();
    sci_[1].reset();
    adc_hs_.reset();
    adc_ms_.reset();
    adc_ms1_.reset();
    if (!by_watchdog) {
      ports_.reset();
      ports7042_.reset();
      flash_.reset();
    }
    cpu_.reset();
    ++resets_;
  }

  ChipConfig cfg_;
  Bus bus_;
  Cpu cpu_;
  emu::IoMux io_;
  emu::Scheduler sched_;
  Intc intc_;
  SysRegs sysregs_;
  Bsc bsc_;
  Dmac dmac_;
  Mtu mtu_;
  Cmt cmt_;
  Wdt wdt_;
  Sci sci_[2];
  AdcHighSpeed adc_hs_;
  AdcMidSpeed adc_ms_, adc_ms1_;
  Ports ports_;
  Ports7042 ports7042_;
  FlashRegs flash_;
  u64 resets_ = 0;
};

}  // namespace sh2

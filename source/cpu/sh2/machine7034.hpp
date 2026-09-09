// Machine7034: bus + CPU + scheduler + on-chip peripherals of the SH7034
// (SH-1), the same shape as the SH7014 Machine.
//
// Module set (SH7032/34 hardware manual): INTC (5), UBC (6, registers only),
// BSC (8), DMAC (9), ITU (10), TPC (11, registers only), WDT (12), SCI x2
// (13), A/D (14, the 8-channel converter with the mid-speed model's timing),
// PFC + I/O ports (15, 16), SBYCR (19).  Address space (table 8.3): A26-A24
// select areas 0-7 of 16 MB, A27 the 8-bit (0) or 16-bit (1) shadow of an
// external area; boards mirror their memories into both.  On-chip: 64 KB mask
// ROM in area 0 (mode 2, both shadows), registers at H'5FFFE00-H'5FFFFFF
// (3 states), the 4 KB RAM in the A27 = 1 half of area 7 (H'FFFF000 by
// convention; mapped as one 64 KB page mirrored over H'F000000-H'FFFFFFF).
// The A27 = 0 half of area 7 is external space (CS7).
#pragma once
#include <algorithm>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/sh2/adc.hpp"
#include "cpu/sh2/bsc7034.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/chip.hpp"
#include "cpu/sh2/cpu.hpp"
#include "cpu/sh2/dmac7034.hpp"
#include "cpu/sh2/intc.hpp"
#include "cpu/sh2/itu.hpp"
#include "cpu/sh2/ports7034.hpp"
#include "cpu/sh2/sci.hpp"
#include "cpu/sh2/wdt.hpp"

namespace sh2 {

class Machine7034 final : public WdtResetSink {
 public:
  static constexpr u32 kSci0 = 0x05FFFEC0u, kSci1 = 0x05FFFEC8u, kAdc = 0x05FFFEE0u, kWdt = 0x05FFFFB8u;
  static constexpr u32 kRomShadow = 0x08000000u, kRamArea = 0x0F000000u;

  explicit Machine7034(u8 mode = 2)
      : cfg_(make_chip_config(ChipModel::SH7034, mode)),
        bus_(),
        cpu_(bus_, cfg_),
        io_(cfg_.regfield_base, cfg_.regfield_size),
        intc_(cpu_, intc_layout(ChipModel::SH7034)),
        bsc_(sched_, cpu_, intc_, bus_),
        dmac_(sched_, cpu_, intc_, bus_, cpu_),
        itu_(sched_, cpu_, intc_),
        wdt_(sched_, cpu_, intc_, kWdt),
        sci_{Sci(sched_, cpu_, intc_, kSci0, {IrqSrc::Eri0, IrqSrc::Rxi0, IrqSrc::Txi0, IrqSrc::Tei0}),
             Sci(sched_, cpu_, intc_, kSci1, {IrqSrc::Eri1, IrqSrc::Rxi1, IrqSrc::Txi1, IrqSrc::Tei1})},
        adc_(sched_, cpu_, intc_, kAdc),
        ports_(cpu_) {
    sched_.set_earlier_hook([](void* c, u64 when) { static_cast<Cpu*>(c)->cut_slice(when); }, &cpu_);
    bus_.map_ram(cfg_.ram_base, cfg_.ram_size, Bus::kClsOnchipRam);
    for (u32 a = kRamArea; a < cfg_.ram_base; a += Bus::kPageSize) bus_.mirror(a, Bus::kPageSize, cfg_.ram_base);
    if (cfg_.rom_enabled()) {
      bus_.map_rom(0, cfg_.rom_size, Bus::kClsOnchipRom);
      bus_.mirror(kRomShadow, cfg_.rom_size, 0);
    }
    bus_.map_device(cfg_.regfield_base, cfg_.regfield_size, &io_, Bus::kClsPeriph16S3);
    bus_.set_noexec(cfg_.regfield_base, cfg_.regfield_size);

    intc_.map(io_);
    bsc_.map(io_);
    dmac_.map(io_);
    itu_.map(io_);
    wdt_.map(io_);
    sci_[0].map(io_);
    sci_[1].map(io_);
    adc_.map(io_);
    ports_.map(io_);

    wdt_.set_reset_sink(this);
    // DMAC transfers clear the requesting module's flag (9.3.2).
    for (IrqSrc s : {IrqSrc::Tgi0a, IrqSrc::Tgi1a, IrqSrc::Tgi2a, IrqSrc::Tgi3a})
      dmac_.attach(s, [this, s] { itu_.dmac_clear(s); });
    dmac_.attach(IrqSrc::Adi, [this] { adc_.dmac_activated(); });
    for (unsigned i = 0; i < 2; ++i) {
      Sci& s = sci_[i];
      const u32 tdr = (i ? kSci1 : kSci0) + 3;
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
  Bsc7034& bsc() { return bsc_; }
  Dmac7034& dmac() { return dmac_; }
  Itu& itu() { return itu_; }
  Wdt& wdt() { return wdt_; }
  Sci& sci(unsigned i) { return sci_[i]; }
  AdcMidSpeed& adc() { return adc_; }
  Ports7034& ports() { return ports_; }
  const ChipConfig& config() const { return cfg_; }

  u64 now() const { return cpu_.total_states(); }

  // IRQ0-IRQ7 pin level (true = low) and NMI.
  void set_irq_pin(unsigned n, bool low) { intc_.set_irq_pin(n, low); }
  void set_nmi_pin(bool high) {
    if (intc_.set_nmi_pin(high)) dmac_.on_nmi();
  }

  void reset() { reset_internal(false); }
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
    if (!by_watchdog) bsc_.reset();  // a manual reset keeps the BSC, PFC and ports (4.2.1)
    dmac_.reset();
    itu_.reset();
    wdt_.reset(by_watchdog);
    sci_[0].reset();
    sci_[1].reset();
    adc_.reset();
    if (!by_watchdog) ports_.reset();
    cpu_.reset();
    ++resets_;
  }

  ChipConfig cfg_;
  Bus bus_;
  Cpu cpu_;
  emu::IoMux io_;
  emu::Scheduler sched_;
  Intc intc_;
  Bsc7034 bsc_;
  Dmac7034 dmac_;
  Itu itu_;
  Wdt wdt_;
  Sci sci_[2];
  AdcMidSpeed adc_;
  Ports7034 ports_;
  u64 resets_ = 0;
};

}  // namespace sh2

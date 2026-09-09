// Machine: bus + CPU + scheduler + on-chip peripherals for one H8/500 device.
//
// Peripheral models attach to the register field through the emu::IoMux and to the
// scheduler for timed events; external device models (LCD, audio...) do the
// same through the bus and scheduler.  Machine::run() advances time in slices
// bounded by the next event, so every device sees the exact state count at
// which its event was due.
#pragma once
#include <algorithm>
#include <memory>

#include "cpu/h8500/adc.hpp"
#include "cpu/h8500/bus.hpp"
#include "cpu/h8500/chip.hpp"
#include "cpu/h8500/cpu.hpp"
#include "cpu/h8500/dtc.hpp"
#include "cpu/h8500/frt.hpp"
#include "cpu/h8500/h8532_regs.hpp"
#include "cpu/h8500/h8570_regs.hpp"
#include "cpu/h8500/intc.hpp"
#include "cpu/h8500/isp_regs.hpp"
#include "common/iomux.hpp"
#include "cpu/h8500/ports.hpp"
#include "cpu/h8500/pwm.hpp"
#include "common/sched.hpp"
#include "cpu/h8500/sci.hpp"
#include "cpu/h8500/tmr.hpp"
#include "cpu/h8500/wdt.hpp"

namespace h8500 {

class Machine final : public ResetSink {
 public:
  Machine(ChipModel model, u8 mode)
      : cfg_(make_chip_config(model, mode)),
        bus_(cfg_.address_bits()),
        cpu_(bus_, cfg_),
        io_(cfg_.regfield_base, cfg_.regfield_size),
        intc_(cpu_, intc_layout_for(cfg_.model)),
        // The H8/532 has three FRTs, at H'FF90/A0/B0, and its 8-bit timer at
        // H'FFD0; the H8/570 has neither (a PWM timer instead).
        frt_{Frt(sched_, cpu_, intc_, is532() ? 0xFF90 : 0xFEA0, {IrqSrc::Frt1Ici, IrqSrc::Frt1Ocia, IrqSrc::Frt1Ocib, IrqSrc::Frt1Fovi}),
             Frt(sched_, cpu_, intc_, is532() ? 0xFFA0 : 0xFEB0, {IrqSrc::Frt2Ici, IrqSrc::Frt2Ocia, IrqSrc::Frt2Ocib, IrqSrc::Frt2Fovi}),
             Frt(sched_, cpu_, intc_, 0xFFB0, {IrqSrc::Frt3Ici, IrqSrc::Frt3Ocia, IrqSrc::Frt3Ocib, IrqSrc::Frt3Fovi})},
        tmr_(sched_, cpu_, intc_, is532() ? 0xFFD0 : 0xFEC0, {IrqSrc::TmrCmia, IrqSrc::TmrCmib, IrqSrc::TmrOvi}),
        // H8/570 addresses differ (appendix B): WDT H'FE8A, RSTCSR H'FF4E,
        // one SCI at H'FE98, A/D at H'FE80.
        // H8/532: WDT TCSR/TCNT H'FFEC (no RSTCSR — the reset enable is in
        // TCSR), one SCI at H'FFD8, A/D at H'FFE0.
        wdt_(sched_, cpu_, intc_, is570() ? 0xFE8A : (is532() ? 0xFFEC : 0xFF10), is570() ? 0xFF4E : (is532() ? 0 : 0xFF1E)),
        sci_{Sci(sched_, cpu_, intc_, is570() ? 0xFE98 : (is532() ? 0xFFD8 : 0xFEC8), {IrqSrc::Sci1Eri, IrqSrc::Sci1Rxi, IrqSrc::Sci1Txi}),
             Sci(sched_, cpu_, intc_, 0xFED0, {IrqSrc::Sci2Eri, IrqSrc::Sci2Rxi, IrqSrc::Sci2Txi})},
        adc_(sched_, cpu_, intc_, is570() ? 0xFE80 : (is532() ? 0xFFE0 : 0xFE90)),
        // The H8/532's port block sits at the bottom of its own register field.
        ports_(cfg_, cfg_.model == ChipModel::H8_532 ? 0xFF80 : 0xFE80),
        sysregs_(cfg_),
        dtc_(bus_, cpu_, intc_),
        pwm_(sched_, cpu_, intc_),
        isp_regs_(intc_),
        sysregs570_(cfg_, intc_),
        sysregs532_(cfg_, intc_) {
    sched_.set_earlier_hook([](void* c, u64 when) { static_cast<Cpu*>(c)->cut_slice(when); }, &cpu_);
    configure_bus(bus_, cfg_);
    bus_.map_device(cfg_.regfield_base, cfg_.regfield_size, &io_, BusClass::W8_S3);
    bus_.set_noexec(cfg_.noexec_base, cfg_.noexec_size);
    intc_.map(io_);
    wdt_.set_reset_sink(this);
    if (cfg_.model == ChipModel::H8_510) {
      frt_[0].map(io_);
      frt_[1].map(io_);
      tmr_.map(io_);
      wdt_.map(io_);
      sci_[0].map(io_);
      sci_[1].map(io_);
      adc_.map(io_);
      ports_.map(io_);
      sysregs_.map(io_);
      // DTC: module flags cleared when a transfer serves the interrupt.
      for (unsigned i = 0; i < 2; ++i) {
        Frt& f = frt_[i];
        const IrqSrc ici = i ? IrqSrc::Frt2Ici : IrqSrc::Frt1Ici;
        const IrqSrc ocia = i ? IrqSrc::Frt2Ocia : IrqSrc::Frt1Ocia;
        const IrqSrc ocib = i ? IrqSrc::Frt2Ocib : IrqSrc::Frt1Ocib;
        dtc_.attach(ici, [&f, ici](u16) { f.dtc_clear(ici); });
        dtc_.attach(ocia, [&f, ocia](u16) { f.dtc_clear(ocia); });
        dtc_.attach(ocib, [&f, ocib](u16) { f.dtc_clear(ocib); });
        Sci& s = sci_[i];
        dtc_.attach(i ? IrqSrc::Sci2Rxi : IrqSrc::Sci1Rxi, [&s](u16) { s.dtc_read_rdr(); });
        dtc_.attach(i ? IrqSrc::Sci2Txi : IrqSrc::Sci1Txi, [&s](u16 d) { s.dtc_wrote_tdr(u8(d)); });
      }
      dtc_.attach(IrqSrc::TmrCmia, [this](u16) { tmr_.dtc_clear(IrqSrc::TmrCmia); });
      dtc_.attach(IrqSrc::TmrCmib, [this](u16) { tmr_.dtc_clear(IrqSrc::TmrCmib); });
      dtc_.attach(IrqSrc::Adi, [this](u16) { adc_.dtc_clear(); });
      intc_.set_dtc_client(&dtc_);
    } else if (cfg_.model == ChipModel::H8_532) {
      ports_.set_port9(0xFFFE);
      adc_.set_channel_select_bits(3);  // eight analog inputs, two scan groups
      for (Frt& f : frt_) f.map(io_);
      tmr_.map(io_);
      wdt_.map(io_);
      sci_[0].map(io_);
      adc_.map(io_);
      ports_.map(io_);
      pwm532_.map(io_);
      sysregs532_.map(io_);
      // RAMCR.RAME is stored but the on-chip RAM stays mapped: with it clear
      // the addresses fall through to the external bus, and only the board
      // knows what is under them — it installs its own hook if it cares.
    } else if (cfg_.model == ChipModel::H8_570) {
      sci_[0].map(io_);
      adc_.map(io_);
      wdt_.map(io_);
      pwm_.map(io_);
      isp_regs_.map(io_);
      ports570_.map(io_);
      sysregs570_.map(io_);
      // The register field is also decoded in the top page of the 1-Mbyte space.
      if (cfg_.max_mode()) bus_.map_device(0xFFE80, cfg_.regfield_size, &io_mirror_, BusClass::W8_S3);
    }
  }

  Bus& bus() { return bus_; }
  const Bus& bus() const { return bus_; }
  Cpu& cpu() { return cpu_; }
  const Cpu& cpu() const { return cpu_; }
  emu::Scheduler& sched() { return sched_; }
  emu::IoMux& io() { return io_; }
  Intc& intc() { return intc_; }
  Frt& frt(unsigned i) { return frt_[i]; }  // 0 = FRT1, 1 = FRT2, 2 = FRT3 (H8/532)
  Tmr& tmr() { return tmr_; }
  Wdt& wdt() { return wdt_; }
  Sci& sci(unsigned i) { return sci_[i]; }  // 0 = SCI1, 1 = SCI2
  Adc& adc() { return adc_; }
  Ports& ports() { return ports_; }
  SysRegs& sysregs() { return sysregs_; }
  Dtc& dtc() { return dtc_; }
  // H8/532 only
  Pwm532& pwm532() { return pwm532_; }
  SysRegs532& sysregs532() { return sysregs532_; }
  // H8/570 only
  Pwm& pwm() { return pwm_; }
  IspRegs& isp_regs() { return isp_regs_; }
  Ports570& ports570() { return ports570_; }
  SysRegs570& sysregs570() { return sysregs570_; }
  const ChipConfig& config() const { return cfg_; }

  // Current time in states (phi clock cycles).
  u64 now() const { return cpu_.total_states(); }

  // External reset (RES pin).
  void reset() { reset_internal(false); }
  // Internal reset generated by a watchdog overflow: same sequence, but
  // RSTCSR keeps WRST/RSTOE so software can tell the two apart.
  void watchdog_reset() override { reset_internal(true); }
  u64 resets() const { return resets_; }

  // Advance the machine by at least `states`, honouring every scheduled event.
  // Returns the number of states actually elapsed.
  u64 run(u64 states) {
    const u64 start = now();
    const u64 end = start + states;
    while (now() < end) {
      sched_.run_due(now());
      dtc_.service();  // data transfers run between instructions and stall the CPU
      cpu_.poll();     // an interrupt raised by an event is taken at this boundary
      const u64 next = std::min(sched_.next_time(), end);
      const u64 want = next > now() ? next - now() : 1;
      cpu_.run(want);
    }
    sched_.run_due(now());
    dtc_.service();
    cpu_.poll();
    return now() - start;
  }

 private:
  void reset_internal(bool by_watchdog) {
    ++resets_;
    intc_.reset();
    for (Frt& f : frt_) f.reset();
    tmr_.reset();
    wdt_.reset(by_watchdog);
    sci_[0].reset();
    sci_[1].reset();
    adc_.reset();
    ports_.reset();
    sysregs_.reset();
    dtc_.reset();
    if (is532()) {
      pwm532_.reset();
      sysregs532_.reset();
    }
    if (is570()) {
      pwm_.reset();
      isp_regs_.reset();
      ports570_.reset();
      sysregs570_.reset();
    }
    cpu_.reset();
  }
  bool is570() const { return cfg_.model == ChipModel::H8_570; }
  bool is532() const { return cfg_.model == ChipModel::H8_532; }

  // The register field seen through another page (H8/570 top page).
  struct IoMirror final : Device {
    explicit IoMirror(emu::IoMux& m) : io(m) {}
    u8 read8(u32 a) override { return io.read8(a & 0xFFFF); }
    void write8(u32 a, u8 v) override { io.write8(a & 0xFFFF, v); }
    u16 read16(u32 a) override { return io.read16(a & 0xFFFF); }
    void write16(u32 a, u16 v) override { io.write16(a & 0xFFFF, v); }
    emu::IoMux& io;
  };

  ChipConfig cfg_;
  Bus bus_;
  Cpu cpu_;
  emu::IoMux io_;
  emu::Scheduler sched_;
  Intc intc_;
  Frt frt_[3];
  Tmr tmr_;
  Wdt wdt_;
  Sci sci_[2];
  Adc adc_;
  Ports ports_;
  SysRegs sysregs_;
  Dtc dtc_;
  Pwm pwm_;
  Pwm532 pwm532_;
  IspRegs isp_regs_;
  Ports570 ports570_;
  SysRegs570 sysregs570_;
  SysRegs532 sysregs532_;
  IoMirror io_mirror_{io_};
  u64 resets_ = 0;
};

}  // namespace h8500

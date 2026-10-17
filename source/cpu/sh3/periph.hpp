// On-chip peripheral block of the SH7709A: the interrupt controller with the
// IRQ0-5 / IRL pins, the PINT port interrupts and the SH7709A's INTEVT2, the cache / MMU control
// registers the core does not own, the user break registers (stored), the
// bus state controller with its refresh counter, the DMA controller, the
// clock pulse generator with the watchdog, the realtime clock, the timer
// unit, the SCI, the two FIFO serial ports (IrDA/SCIF1 and SCIF2), the
// compare match timer, the A/D and D/A converters and the I/O ports.
//
// Registers live in two windows: the P4 block at H'FFFFFE80-H'FFFFFFFF and
// the area-1 block at physical H'04000000 (reached as H'A4000000).  Every
// register has a natural width (8, 16 or 32 bits); an access of another
// width is composed from or merged into the natural one, big-endian.  Timed
// units keep one scheduler event for their next observable transition and
// synchronise lazily when read; nothing is polled per instruction.
#pragma once
#include <array>
#include <deque>
#include <functional>

#include "common/sched.hpp"
#include "cpu/sh3/bus.hpp"
#include "cpu/sh3/cpu.hpp"

namespace sh3 {

// Interrupt sources in the manual's default priority order (table 6.x):
// among sources at the same IPR level the earlier entry wins.
enum Source : u8 {
  kSrcNmi = 0,
  kSrcIrq0, kSrcIrq1, kSrcIrq2, kSrcIrq3, kSrcIrq4, kSrcIrq5,  // independent IRQ pins (ICR1.IRQLVL = 0)
  kSrcIrlLevel,                                                // encoded IRL3-0 level (IRQLVL = 1)
  kSrcPint07, kSrcPint815,
  kSrcDei0, kSrcDei1, kSrcDei2, kSrcDei3,
  kSrcIrdaEri, kSrcIrdaRxi, kSrcIrdaBri, kSrcIrdaTxi,
  kSrcScifEri, kSrcScifRxi, kSrcScifBri, kSrcScifTxi,
  kSrcAdi,
  kSrcTuni0, kSrcTuni1, kSrcTuni2, kSrcTicpi2,
  kSrcAti, kSrcPri, kSrcCui,
  kSrcSciEri, kSrcSciRxi, kSrcSciTxi, kSrcSciTei,
  kSrcIti, kSrcRcmi, kSrcRovi,
  kSourceCount
};

class Peripherals final : public IntcBlock {
 public:
  using ByteHook = std::function<void(u8)>;
  // I/O ports: the board supplies the input levels of a port (bits set where
  // the pin reads high) and is told every data / control register write.
  // Port index 0-10 = ports A-H, J-L (there is no port I), 11 = SCP.
  static constexpr unsigned kPortCount = 12;
  using PortInHook = std::function<u8(unsigned port)>;
  using PortOutHook = std::function<void(unsigned port, u8 data, u16 ctrl)>;

  // cpu_clock_hz: the CPU (I-phi) clock.  frqcr_reset: the FRQCR value the
  // mode pins select at reset; its IFC / PFC fields set the peripheral clock
  // relative to the CPU clock until the firmware rewrites FRQCR.
  Peripherals(emu::Scheduler& sched, Cpu& cpu, Bus& bus, u64 cpu_clock_hz, u16 frqcr_reset);
  ~Peripherals() override;

  // Register devices into the P4 space and area 1.
  void map(Bus& bus);
  void reset();

  // --- IntcBlock -------------------------------------------------------------------
  int pending_priority(u32& intevt, u32& intevt2, unsigned& source) const override;
  void acknowledge(unsigned source) override;
  bool nmi_ignores_bl() const override { return (icr1_ & 0x2000) != 0; }

  // --- pins --------------------------------------------------------------------------
  // IRQ0-5 pin levels (ICR1 selects edge / level sensing per pin).
  void set_irq_pin(unsigned pin, bool high);
  // PINT0-15 (the port C / port F pins): level sensed against ICR2 (bit set =
  // high level requests), enabled per pin by PINTER.
  void set_pint_pin(unsigned pin, bool high);
  // Encoded IRL3-0 (ICR1.IRQLVL set): level 0-14 requests, 15 = no request.
  void set_irl_level(unsigned level);
  void set_nmi_pin(bool high);

  // --- serial ---------------------------------------------------------------------------
  // A received byte takes one frame time to arrive: by default the time the
  // port's SMR / BRR settings give (sections 14.2.8, 15.2.9), or the fixed
  // number of states a board forces with set_*_byte_states (0 to go back to
  // the baud-rate generator).
  void sci_receive(const u8* data, size_t len);
  void sci_receive(u8 b) { sci_receive(&b, 1); }
  void set_sci_tx_hook(ByteHook h) { sci_.tx_hook = std::move(h); }
  void set_sci_byte_states(u32 s) { sci_.byte_states = s; }
  u32 sci_frame_states() const { return frame_states(sci_); }
  // FIFO ports: channel 0 = IrDA (SCIF1 at H'A4000140), 1 = SCIF2 (H'A4000150).
  void scif_receive(unsigned ch, const u8* data, size_t len);
  void scif_receive(unsigned ch, u8 b) { scif_receive(ch, &b, 1); }
  void set_scif_tx_hook(unsigned ch, ByteHook h) { scif_[ch & 1].tx_hook = std::move(h); }
  void set_scif_byte_states(unsigned ch, u32 s) { scif_[ch & 1].byte_states = s; }
  u32 scif_frame_states(unsigned ch) const { return frame_states(scif_[ch & 1]); }

  // --- DMA ------------------------------------------------------------------------------
  // One DREQ burst on an external-request channel: the whole remaining
  // block moves now, TE and DEI follow.  Returns false when not armed.
  bool dma_ext_request(unsigned channel);

  // --- ports / analogue ------------------------------------------------------------------
  void set_port_hooks(PortInHook in, PortOutHook out) { port_in_ = std::move(in); port_out_ = std::move(out); }
  u8 port_data(unsigned port) const { return port < kPortCount ? pdr_[port] : 0; }
  u16 port_ctrl(unsigned port) const { return port < kPortCount ? pcr_[port] : 0; }
  // 10-bit A/D input of channel 0-7.
  void set_adc_input(unsigned ch, u16 value) { adc_in_[ch & 7] = value & 0x3FF; }

  // --- clocks ------------------------------------------------------------------------------
  u64 cpu_clock_hz() const { return cpu_clock_hz_; }
  u32 peripheral_divider() const { return pclk_div_; }  // CPU states per peripheral clock
  u32 bus_divider() const { return bclk_div_; }
  u16 frqcr() const { return frqcr_; }

  // Introspection for tests and boards.
  u32 tmu_count(unsigned ch);
  u8 wdt_count();
  u16 icr0() const { return u16(icr0_ | (nmi_high_ ? 0x8000 : 0)); }
  u16 icr1() const { return icr1_; }
  u16 ipr(unsigned i) const { return ipr_[i % 5]; }  // 0-4 = IPRA-IPRE
  u8 irr0() const { return irr0_; }

 private:
  enum Module : u8 { kRegs, kSdmr, kModuleCount };

  struct RegDevice final : emu::Device {
    RegDevice(Peripherals& p, Module m) : p(p), module(m) {}
    u8 read8(u32 a) override { return p.reg_read8(module, a); }
    u16 read16(u32 a) override { return p.reg_read16(module, a); }
    u32 read32(u32 a) override { return p.reg_read32(module, a); }
    void write8(u32 a, u8 v) override { p.reg_write8(module, a, v); }
    void write16(u32 a, u16 v) override { p.reg_write16(module, a, v); }
    void write32(u32 a, u32 v) override { p.reg_write32(module, a, v); }
    Peripherals& p;
    Module module;
  };

  u64 now() const { return cpu_.now(); }
  void update_irq() { cpu_.reeval_irq(); }
  void cancel(emu::Scheduler::EventId& e) { if (e) { sched_.cancel(e); e = 0; } }

  // A register at `a` with natural width `size` (1, 2 or 4): false when no
  // register of that width sits there.  The wrappers compose other widths.
  bool reg_read(u32 a, unsigned size, u32& v);
  bool reg_write(u32 a, unsigned size, u32 v);
  bool p4_read(u32 a, unsigned size, u32& v);
  bool p4_write(u32 a, unsigned size, u32 v);
  bool area1_read(u32 off, unsigned size, u32& v);
  bool area1_write(u32 off, unsigned size, u32 v);
  u8 reg_read8(Module m, u32 a);
  u16 reg_read16(Module m, u32 a);
  u32 reg_read32(Module m, u32 a);
  void reg_write8(Module m, u32 a, u8 v);
  void reg_write16(Module m, u32 a, u16 v);
  void reg_write32(Module m, u32 a, u32 v);

  // interrupt controller
  void request(unsigned source);
  void withdraw(unsigned source);
  void set_level(unsigned source, bool asserted) { if (asserted) request(source); else withdraw(source); }
  void recompute_priorities();
  void resample_irq_pins();
  void update_pint();
  unsigned irq_sense(unsigned pin) const { return (icr1_ >> (pin * 2)) & 3; }
  bool irl_mode() const { return (icr1_ & 0x4000) != 0; }
  u8 irr1() const;
  u8 irr2() const;

  // ports
  u8 port_read(unsigned port) const;

  // BSC refresh counter
  void refresh_sync();
  u32 refresh_divider() const;
  void refresh_reschedule();
  static void on_refresh(void* self, u64 when, u64 now);

  // DMAC
  struct Dma {
    u32 sar = 0, dar = 0, tcr = 0, chcr = 0;
    emu::Scheduler::EventId event = 0;
    Peripherals* owner = nullptr;
    unsigned index = 0;
  };
  void dmac_check(unsigned ch);
  void dma_transfer(unsigned ch, bool delayed_completion);
  void dma_complete(unsigned ch);
  static void on_dma(void* dma, u64, u64);

  // CPG / WDT
  void apply_frqcr(u16 v);
  u32 wdt_states_per_count() const;
  void wdt_sync(u64 t);
  void wdt_reschedule();
  static void on_wdt(void* self, u64 when, u64 now);

  // RTC
  void rtc_sync();

  // TMU
  struct Timer {
    u32 tcor = 0xFFFFFFFFu, tcnt = 0xFFFFFFFFu;
    u16 tcr = 0;
    u64 synced = 0;
    u32 presc = 0;  // states into the current count period
    emu::Scheduler::EventId event = 0;
    Peripherals* owner = nullptr;
    unsigned index = 0;
  };
  bool tmu_running(unsigned ch) const { return (tstr_ >> ch) & 1; }
  u32 tmu_states_per_count(unsigned ch) const;
  void tmu_sync(unsigned ch, u64 t);
  void tmu_reschedule(unsigned ch);
  void tmu_update_level(unsigned ch);
  static void on_tmu(void* timer, u64, u64);

  // CMT
  void cmt_sync();
  u32 cmt_states_per_count() const;

  // A/D
  void adc_start();
  static void on_adc(void* self, u64, u64);
  void adc_complete();

  // SCI / SCIF
  struct Serial {
    u8 smr = 0, brr = 0xFF, scr = 0, tdr = 0xFF, ssr = 0x84, rdr = 0, scmr = 0;  // SCI registers
    u16 fsr = 0x0060, fcr = 0, fdr = 0;                                          // SCIF registers
    std::deque<u8> queue;  // bytes offered by the board, not yet in the receiver
    std::deque<u8> fifo;   // SCIF receive FIFO
    bool rx_active = false;
    u32 byte_states = 0;   // frame time a board forces; 0: from SMR / BRR
    emu::Scheduler::EventId rx_event = 0;
    ByteHook tx_hook;
    Peripherals* owner = nullptr;
    unsigned index = 0;
  };
  u32 frame_states(const Serial& s) const;
  void sci_update_levels();
  void sci_try_start_rx(u64 at);
  static void on_sci_rx(void* self, u64 when, u64 now);
  void scif_update_levels(unsigned ch);
  void scif_try_start_rx(unsigned ch, u64 at);
  static void on_scif_rx(void* serial, u64 when, u64 now);
  bool scif_read(unsigned ch, u32 off, unsigned size, u32& v);
  bool scif_write(unsigned ch, u32 off, unsigned size, u32 v);

  emu::Scheduler& sched_;
  Cpu& cpu_;
  Bus& bus_;
  u64 cpu_clock_hz_;
  u32 pclk_div_ = 4, bclk_div_ = 1;
  u16 frqcr_reset_;
  std::array<RegDevice, kModuleCount> devs_;

  // INTC
  u16 icr0_ = 0, icr1_ = 0x4000, icr2_ = 0, pinter_ = 0;
  u16 ipr_[5] = {};  // IPRA-IPRE
  u8 irr0_ = 0;
  u64 requesting_ = 0;
  std::array<u8, kSourceCount> priority_{};
  bool irq_high_[6] = {true, true, true, true, true, true};
  u16 pint_high_ = 0xFFFF;
  unsigned irl_level_ = 15;
  bool nmi_high_ = true;

  // CCN / UBC / UDI storage
  u32 pteh_ = 0, ptel_ = 0, ttb_ = 0, tea_ = 0, mmucr_ = 0, ccr_ = 0, ccr2_ = 0;
  u8 basra_ = 0, basrb_ = 0;
  u32 ubc_[12] = {};
  u16 sdir_ = 0xFFFF;

  // BSC
  u16 bcr1_ = 0, bcr2_ = 0x3FF0, bcr3_ = 0, wcr1_ = 0x3FF3, wcr2_ = 0xFFFF, mcr_ = 0, dcr_ = 0, bsc_pcr_ = 0;
  u16 rtcsr_ = 0, rtcnt_ = 0, rtcor_ = 0, rfcr_ = 0;
  u64 refresh_synced_ = 0;
  u32 refresh_presc_ = 0;
  emu::Scheduler::EventId refresh_event_ = 0;

  // DMAC
  std::array<Dma, 4> dma_{};
  u16 dmaor_ = 0;

  // CPG / WDT
  u16 frqcr_ = 0;
  u8 stbcr_ = 0, stbcr2_ = 0, wtcsr_ = 0, wtcnt_ = 0;
  u64 wdt_synced_ = 0;
  u32 wdt_presc_ = 0;
  emu::Scheduler::EventId wdt_event_ = 0;

  // RTC: R64CNT, seconds, minutes, hours, weekday, day, month, year (BCD), alarms
  u8 rtc_[16] = {};
  u8 rcr1_ = 0, rcr2_ = 0x09;
  u64 rtc_synced_ = 0, rtc_frac_ = 0;

  // TMU
  u8 tocr_ = 0, tstr_ = 0;
  std::array<Timer, 3> tmu_{};
  u32 tcpr2_ = 0;

  // CMT
  u16 cmstr_ = 0, cmcsr_ = 0, cmcnt_ = 0, cmcor_ = 0xFFFF;
  u64 cmt_synced_ = 0;
  u32 cmt_presc_ = 0;

  // A/D, D/A
  u8 adcsr_ = 0, adcr_ = 0x07;
  u16 addr_[4] = {};
  u16 adc_in_[8] = {};
  emu::Scheduler::EventId adc_event_ = 0;
  u8 dadr_[2] = {}, dacr_ = 0x1F;

  // ports
  u16 pcr_[kPortCount] = {};
  u8 pdr_[kPortCount] = {};
  PortInHook port_in_;
  PortOutHook port_out_;

  // serial
  Serial sci_;
  std::array<Serial, 2> scif_{};
};

}  // namespace sh3

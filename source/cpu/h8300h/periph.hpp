// On-chip peripheral block of the H8/3002: interrupt controller and system
// registers (with the bus controller), the 16-bit integrated timer unit
// with its programmable timing pattern controller, the refresh controller,
// the watchdog, the two serial communication interfaces and the DMA
// controller.  The A/D converter and the I/O ports are left to the board,
// which claims their registers in the register field (machine.hpp).
//
// Modelled to the depth the Roland firmware in this tree exercises.  Every
// timed unit keeps one scheduler event for its next observable transition;
// nothing is polled per instruction.  Interrupt requests are levels held by
// the modules (a flag and its enable) or edges latched by the controller
// (the external IRQ pins, the DMA end).
#pragma once
#include <array>
#include <deque>
#include <functional>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/h8300h/bus.hpp"
#include "cpu/h8300h/cpu.hpp"

namespace h8300h {

class ResetSink {
 public:
  virtual ~ResetSink() = default;
  virtual void watchdog_reset() = 0;
};

class Peripherals final : public IntcBlock {
 public:
  using ByteHook = std::function<void(u8)>;
  using TpcHook = std::function<void(u16 value, u16 mask)>;

  Peripherals(emu::Scheduler& sched, Cpu& cpu, Bus& bus, emu::IoMux& io);
  ~Peripherals() override;

  void set_reset_sink(ResetSink* s) { reset_sink_ = s; }
  // Mode pins (1-4): the bus controller's reset image depends on them.
  void set_operating_mode(int mode) { mode_ = mode; }
  int operating_mode() const { return mode_; }
  // Reset image of every module (the machine calls it on reset).
  void reset();

  // --- IntcBlock -------------------------------------------------------------------
  bool arbitrate(unsigned& which, u8 ccr) const override;
  void acknowledge(unsigned which) override;
  bool standby_wake(u8 ccr) const override;
  u8 syscr() const override { return sys_[kSyscr & 0xFF]; }
  void enter_software_standby() override;

  // --- interrupt requests ------------------------------------------------------------
  // An edge request (the external IRQ pins, a board-generated request): the
  // DMAC gets the first refusal, else it is latched until accepted.
  void request(unsigned which);
  // A level (a module flag with its enable): asserted or withdrawn.
  void set_level(unsigned which, bool asserted);
  // The old interface: assert = request(), deassert = clear the latch.
  void set_request(unsigned which, bool asserted) { if (asserted) request(which); else clear_request(which); }
  void clear_request(unsigned which);
  u64 pending() const { return pending_ | level_; }
  bool requested(unsigned which) const { return ((pending_ | level_) >> which) & 1; }

  // --- ITU pins ----------------------------------------------------------------------
  void tick_extclock(unsigned channel);
  void capture_a(unsigned channel);
  // Call only on a pin transition; TIOR selects which edges capture.
  void capture_a_edge(unsigned channel, bool high);
  void set_tpc_output_hook(TpcHook h) { tpc_hook_ = std::move(h); }
  u16 tcnt(unsigned channel);

  // --- SCI --------------------------------------------------------------------------------
  void receive(unsigned channel, const u8* data, size_t len);
  void receive(unsigned channel, u8 byte) { receive(channel, &byte, 1); }
  void set_tx_hook(unsigned channel, ByteHook h) { if (channel < 2) sci_[channel].tx_hook = std::move(h); }
  // Every TDR write (a board with a shift register on the TXD pin).
  void set_tdr_write_hook(unsigned channel, ByteHook h) { if (channel < 2) sci_[channel].tdr_hook = std::move(h); }
  size_t rx_queued(unsigned channel) const { return channel < 2 ? sci_[channel].queue.size() : 0; }

  // --- DMAC --------------------------------------------------------------------------------
  void request_dreq(bool high);

  // --- registers (for boards and harnesses) --------------------------------------------------
  u8 sysreg(u32 addr) const { return sys_[addr & 0xFF]; }

 private:
  struct Module : emu::Device {
    explicit Module(Peripherals& p) : p(p) {}
    Peripherals& p;
  };
  struct SysDevice final : Module { using Module::Module; u8 read8(u32 a) override { return p.sys_read(a); } void write8(u32 a, u8 v) override { p.sys_write(a, v); } };
  struct ItuDevice final : Module { using Module::Module; u8 read8(u32 a) override { return p.itu_read(a); } void write8(u32 a, u8 v) override { p.itu_write(a, v); } };
  struct TpcDevice final : Module { using Module::Module; u8 read8(u32 a) override { return p.tpc_read(a); } void write8(u32 a, u8 v) override { p.tpc_write(a, v); } };
  struct RfshDevice final : Module { using Module::Module; u8 read8(u32 a) override { return p.rfsh_read(a); } void write8(u32 a, u8 v) override { p.rfsh_write(a, v); } };
  struct WdtDevice final : Module { using Module::Module; u8 read8(u32 a) override { return p.wdt_read(a); } void write8(u32 a, u8 v) override { p.wdt_write(a, v); } };
  struct SciDevice final : Module { SciDevice(Peripherals& p, unsigned ch) : Module(p), ch(ch) {} u8 read8(u32 a) override { return p.sci_read(ch, a); } void write8(u32 a, u8 v) override { p.sci_write(ch, a, v); } unsigned ch; };
  struct DmaDevice final : Module { DmaDevice(Peripherals& p, unsigned n) : Module(p), n(n) {} u8 read8(u32 a) override { return p.dma_read(n, a); } void write8(u32 a, u8 v) override { p.dma_write(n, a, v); } unsigned n; };

  u64 now() const { return cpu_.now(); }
  void update_irq() { cpu_.reeval_irq(); }
  void cancel(emu::Scheduler::EventId& e) { if (e) { sched_.cancel(e); e = 0; } }
  static int priority_bit(unsigned which);
  bool high_priority(unsigned which) const;
  bool acceptable(unsigned which, u8 ccr) const;
  void apply_bus_controller();

  // system registers
  u8 sys_read(u32 a);
  void sys_write(u32 a, u8 v);

  // ITU
  struct Channel {
    u16 tcnt = 0, gra = 0xFFFF, grb = 0xFFFF, bra = 0xFFFF, brb = 0xFFFF;
    u8 tcr = 0x80, tior = 0x88, tsr = 0xF8, tier = 0xF8, tsr_read_mask = 0;
    u64 synced = 0;
    emu::Scheduler::EventId event = 0;
    Peripherals* owner = nullptr;
    unsigned index = 0;
  };
  void itu_reset_channel(unsigned ch);
  u8 itu_read(u32 a);
  void itu_write(u32 a, u8 v);
  bool itu_running(unsigned ch) const { return (tstr_ & (1u << ch)) && !(itu_[ch].tcr & 4); }
  void itu_sync(unsigned ch, u64 t);
  void itu_sync_all(u64 t) { for (unsigned i = 0; i < 5; ++i) itu_sync(i, t); }
  void itu_reschedule(unsigned ch);
  void itu_clock(unsigned ch);  // one count clock
  void itu_update_levels();
  static void on_itu(void* channel, u64, u64);
  // TPC
  u8 tpc_read(u32 a);
  void tpc_write(u32 a, u8 v);
  void tpc_compare(unsigned channel, bool b);
  bool tpc_same_trigger(int low) const { return ((tpcr_ >> (low * 2)) & 3) == ((tpcr_ >> ((low + 1) * 2)) & 3); }
  void tpc_notify();
  // refresh controller
  u8 rfsh_read(u32 a);
  void rfsh_write(u32 a, u8 v);
  void rfsh_sync(u64 t);
  void rfsh_reschedule();
  void rfsh_update_level() { set_level(kVecCmi, rfsh_cmie_ && (rfsh_[1] & 0x80)); }
  static void on_rfsh(void* self, u64, u64);
  // watchdog
  u8 wdt_read(u32 a);
  void wdt_write(u32 a, u8 v);
  void wdt_sync(u64 t);
  void wdt_reschedule();
  void wdt_update_level() { set_level(kVecWovi, (wdt_tcsr_ & 0xC0) == 0x80); }
  static void on_wdt(void* self, u64, u64);
  // SCI
  struct Sci {
    u8 data[8] = {};  // SMR, BRR
    u8 scr = 0, tdr = 0xFF, rdr = 0, ssr = 0x84, ssr_read_mask = 0;
    u8 tx_shift = 0, rx_shift = 0;
    bool tx_active = false, rx_active = false;
    std::deque<u8> queue;
    emu::Scheduler::EventId tx_event = 0, rx_event = 0;
    ByteHook tx_hook, tdr_hook;
  };
  u8 sci_read(unsigned ch, u32 a);
  void sci_write(unsigned ch, u32 a, u8 v);
  u8 sci_dma_read(unsigned ch, u32 a);
  void sci_dma_write(unsigned ch, u32 a, u8 v);
  u32 sci_frame_states(const Sci& s) const;
  void sci_start_tx(unsigned ch, u64 at);
  void sci_try_start_rx(unsigned ch, u64 at);
  void sci_update_levels(unsigned ch);
  void sci_reset(unsigned ch);
  void on_sci_tx(unsigned ch, u64 when);
  void on_sci_rx(unsigned ch, u64 when);
  static void on_sci_tx0(void* self, u64, u64);
  static void on_sci_tx1(void* self, u64, u64);
  static void on_sci_rx0(void* self, u64, u64);
  static void on_sci_rx1(void* self, u64, u64);
  // DMAC
  struct Dma {
    u8 regs[16] = {};
    bool dtcr_read[2] = {false, false};
  };
  u8 dma_read(unsigned n, u32 a);
  void dma_write(unsigned n, u32 a, u8 v);
  bool dma_offer(unsigned which);  // returns true when a channel consumed the request
  void dma_reset(unsigned n);
  bool dma_full_mode(const Dma& d) const { return (d.regs[7] & 6) == 6; }
  bool dma_full_enabled(const Dma& d) const { return (d.regs[7] & 0x80) && (d.regs[15] & 0x80); }
  static int dma_source_vector(int selection);
  u32 dma_mar(const Dma& d, int sub) const;
  void dma_set_mar(Dma& d, int sub, u32 v);
  u16 dma_count(const Dma& d, int sub) const;
  void dma_set_count(Dma& d, int sub, u16 v);
  void dma_transfer_value(u32 from, u32 to, int size);
  void dma_complete(unsigned n, int sub);
  void dma_transfer_short(unsigned n, int sub);
  void dma_attempt_auto(unsigned n);
  void dma_transfer_full_normal(unsigned n, bool entire);
  void dma_transfer_full_block(unsigned n);
  u8 dma_read8(u32 a);
  void dma_write8(u32 a, u8 v);

  emu::Scheduler& sched_;
  Cpu& cpu_;
  Bus& bus_;
  emu::IoMux& io_;
  ResetSink* reset_sink_ = nullptr;
  int mode_ = 4;

  SysDevice sys_dev_{*this};
  ItuDevice itu_dev_{*this};
  TpcDevice tpc_dev_{*this};
  RfshDevice rfsh_dev_{*this};
  WdtDevice wdt_dev_{*this};
  SciDevice sci_dev_[2]{{*this, 0}, {*this, 1}};
  DmaDevice dma_dev_[2]{{*this, 0}, {*this, 1}};

  // interrupt controller / system registers
  u64 pending_ = 0, level_ = 0;
  std::array<u8, 256> sys_{};
  u8 isr_read_mask_ = 0;

  // ITU
  std::array<Channel, 5> itu_{};
  u8 tstr_ = 0xE0, tsnc_ = 0xE0, tmdr_ = 0x80, tfcr_ = 0xC0, toer_ = 0xFF, tocr_ = 0xFF;
  std::array<u8, 64> itu_space_{};
  // TPC
  u8 tpmr_ = 0xF0, tpcr_ = 0xFF, ndera_ = 0, nderb_ = 0;
  u8 tpc_next_[4] = {};
  u16 tpc_output_ = 0;
  TpcHook tpc_hook_;
  // refresh
  u8 rfsh_[4] = {2, 7, 0, 0xFF};
  u64 rfsh_synced_ = 0;
  u32 rfsh_shift_ = 0;
  bool rfsh_cmie_ = false, rfsh_cmf_read_ = false;
  emu::Scheduler::EventId rfsh_event_ = 0;
  // watchdog
  u8 wdt_tcsr_ = 0x18, wdt_tcnt_ = 0, wdt_rstcsr_ = 0x3F, wdt_pw_tcsr_ = 0, wdt_pw_rst_ = 0;
  bool wdt_tcsr_read_ = false, wdt_rstcsr_read_ = false;
  u64 wdt_synced_ = 0;
  emu::Scheduler::EventId wdt_event_ = 0;
  // SCI
  std::array<Sci, 2> sci_{};
  // DMAC
  std::array<Dma, 2> dma_{};
  bool dreq_high_ = true;
};

}  // namespace h8300h

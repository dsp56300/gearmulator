// On-chip peripheral block of the H8S/2320: the interrupt controller
// (interrupt control modes 0 and 2, IPRA-IPRK), the system and bus controller
// registers, the DMA controller in short address mode, the 16-bit timer pulse
// unit, the serial communication interfaces, the A/D converter and the I/O
// ports.  Sections are those of the H8S/2329 group hardware manual, which the
// ROMless H8S/2320 belongs to.
//
// Modelled to the depth the firmware in this tree exercises, on the shared
// event scheduler like the H8/3002's block (periph.hpp): every timed unit keeps
// one event for its next observable transition, nothing is polled per
// instruction.  Interrupt requests are levels held by the modules (a flag and
// its enable) or edges latched by the controller (the DMA end).  Registers no
// module claims read back what was written.
//
// Not here: the DTC, the 8-bit timers, the watchdog, the IRQ pins, the TPU's
// external clocks and input capture, the DMAC's full and single address modes
// (reported once), the A/D converter's external triggers.
#pragma once
#include <array>
#include <deque>
#include <functional>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/h8300h/bus.hpp"
#include "cpu/h8300h/cpu.hpp"

namespace h8300h {

class Peripherals2320 final : public IntcBlock {
 public:
  static constexpr u32 kRegBase = 0xFFFE00, kRegSize = 0x200;  // the register field
  static constexpr u32 kRamBase = 0xFFEC00, kRamSize = 0x1000;  // the on-chip RAM
  // Bus access classes: the eight areas are 0-7 (kClsArea0); 9 is the
  // H8/3002 register field's, whose timing the core special-cases.
  static constexpr u8 kClsRam = 8, kClsRegisters = 10;
  static constexpr unsigned kVectors = 128, kNmi = 7;
  static constexpr unsigned kSciCount = 3, kAnalogInputs = 8;

  enum Port : unsigned { P1 = 0, P2, P3, P4, P5, P6, PA = 9, PB, PC, PD, PE, PF, PG, kPortCount = 16 };

  using ByteHook = std::function<void(u8)>;
  using Exchange = std::function<u8(u8)>;
  using PortInput = std::function<u8(Port)>;
  using PortWrite = std::function<void(Port, u8)>;

  Peripherals2320(emu::Scheduler& sched, Cpu& cpu, Bus& bus, emu::IoMux& io);
  ~Peripherals2320() override;

  // Reset image of every module, and the bus timing that goes with it.
  void reset();

  // --- IntcBlock -----------------------------------------------------------------------------
  // `mask` is the CCR in interrupt control mode 0 and the EXR in mode 2.
  bool arbitrate(unsigned& which, u8 mask) const override;
  void acknowledge(unsigned which) override;
  bool standby_wake(u8 mask) const override;
  // SBYCR: SSBY and STS2-0 sit where the core looks for them in the H8/3002's SYSCR.
  u8 syscr() const override { return sbycr_; }
  void enter_software_standby() override;
  unsigned control_mode() const override { return ((syscr_ >> 4) & 3) == 2 ? 2 : 0; }
  u8 level(unsigned which) const override;

  // --- interrupt requests ------------------------------------------------------------------------
  void request(unsigned which);  // an edge, latched until accepted
  void set_level(unsigned which, bool asserted);
  bool requested(unsigned which) const;
  // for reports: every request asserted now, and the last one the CPU accepted
  std::array<u64, 2> requests() const { return {pending_[0] | level_[0], pending_[1] | level_[1]}; }
  unsigned last_accepted() const { return last_accepted_; }

  // --- SCI -----------------------------------------------------------------------------------------
  // A byte arriving on RxD; one is clocked in a character time after the one before.
  void receive(unsigned ch, u8 byte);
  size_t rx_queued(unsigned ch) const { return ch < kSciCount ? sci_[ch].queue.size() : 0; }
  // Every byte the moment it has left the transmit shift register.
  void set_tx_hook(unsigned ch, ByteHook h) { if (ch < kSciCount) sci_[ch].tx_hook = std::move(h); }
  // Clocked synchronous: every byte shifted out shifts one in, which the other end answers with.
  void set_peer(unsigned ch, Exchange f) { if (ch < kSciCount) sci_[ch].peer = std::move(f); }
  u64 transmitted(unsigned ch) const { return ch < kSciCount ? sci_[ch].transmitted : 0; }

  // --- A/D converter ---------------------------------------------------------------------------------
  void set_analog(unsigned ch, u16 value);  // what ANn converts to: 0 (AVSS) to 1023 (Vref)

  // --- ports ------------------------------------------------------------------------------------------
  // What the board puts on a port's pins, asked on every PORTn read; the output bits read what they drive.
  void set_port_input(PortInput f) { port_input_ = std::move(f); }
  void set_port_write_hook(PortWrite f) { port_write_ = std::move(f); }
  u8 port_output(Port p) const { return u8(pdr_[p] & pddr_[p]); }

  // --- registers ----------------------------------------------------------------------------------------
  // The register file behind every byte no module claims (a harness can wrap it to count accesses).
  emu::Device& register_file() { return file_dev_; }
  u8 reg(u32 addr) const { return file_[(addr - kRegBase) & (kRegSize - 1)]; }

 private:
  struct Module : emu::Device {
    explicit Module(Peripherals2320& p) : p(p) {}
    Peripherals2320& p;
  };
  struct FileDevice final : Module { using Module::Module; u8 read8(u32 a) override { return p.file_[(a - kRegBase) & (kRegSize - 1)]; } void write8(u32 a, u8 v) override { p.file_[(a - kRegBase) & (kRegSize - 1)] = v; } };
  struct SysDevice final : Module { using Module::Module; u8 read8(u32 a) override { return p.sys_read(a); } void write8(u32 a, u8 v) override { p.sys_write(a, v); } };
  struct DmaDevice final : Module { using Module::Module; u8 read8(u32 a) override { return p.dma_read(a); } void write8(u32 a, u8 v) override { p.dma_write(a, v); } };
  struct TpuDevice final : Module { using Module::Module; u8 read8(u32 a) override { return p.tpu_read(a); } void write8(u32 a, u8 v) override { p.tpu_write(a, v); } };
  struct SciDevice final : Module { using Module::Module; u8 read8(u32 a) override { return p.sci_read(a); } void write8(u32 a, u8 v) override { p.sci_write(a, v); } };
  struct AdcDevice final : Module { using Module::Module; u8 read8(u32 a) override { return p.adc_read(a); } void write8(u32 a, u8 v) override { p.adc_write(a, v); } };
  struct PortDevice final : Module { using Module::Module; u8 read8(u32 a) override { return p.port_read(a); } void write8(u32 a, u8 v) override { p.port_write(a, v); } };

  u64 now() const { return cpu_.now(); }
  void update_irq() { cpu_.reeval_irq(); }
  void cancel(emu::Scheduler::EventId& e) { if (e) { sched_.cancel(e); e = 0; } }
  bool bit(const std::array<u64, 2>& v, unsigned which) const { return (v[which >> 6] >> (which & 63)) & 1; }

  // interrupt controller, system and bus controller registers
  u8 sys_read(u32 a);
  void sys_write(u32 a, u8 v);
  void apply_bus_controller();

  // DMAC (section 7), short address mode: channels 0A, 0B, 1A, 1B, in priority order
  enum DmaSource : u8 {
    kDmaAdi = 1, kDmaSci0Txi = 4, kDmaSci0Rxi = 5, kDmaSci1Txi = 6, kDmaSci1Rxi = 7, kDmaTpu0A = 8,
  };
  struct DmaChannel {
    u32 mar = 0;
    u16 ioar = 0, etcr = 0;
  };
  u8 dma_read(u32 a);
  void dma_write(u32 a, u8 v);
  bool dma_dte(unsigned ch) const { return (dmabcrl_ >> (4 + ch)) & 1; }
  bool dma_unsupported(unsigned ch);
  bool dma_activate(DmaSource source);  // true: a channel with DTA set took the request
  void dma_transfer(unsigned ch);
  void dma_update_end();
  void dma_reoffer();  // a channel may have been enabled for a source that is already asserted
  u8 dma_read8(u32 a);
  void dma_write8(u32 a, u8 v);

  // TPU (section 10): six channels, internal clocks
  struct TpuChannel {
    u8 tcr = 0, tmdr = 0, tiorh = 0, tiorl = 0, tier = 0x40, tsr = 0xC0;
    u16 tcnt = 0;
    u16 tgr[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    u64 synced = 0;
    emu::Scheduler::EventId event = 0;
    Peripherals2320* owner = nullptr;
    unsigned index = 0;
  };
  static int tpu_channel_of(u32 a, u32& offset);
  static unsigned tpu_tgr_count(unsigned ch) { return (ch == 0 || ch == 3) ? 4 : 2; }
  int tpu_shift(unsigned ch) const;
  bool tpu_running(unsigned ch) const { return (tstr_ & (1u << ch)) && tpu_shift(ch) >= 0; }
  bool tpu_cleared_by(unsigned ch, unsigned tgr) const;
  u32 tpu_clocks_to_event(unsigned ch) const;
  void tpu_clock(unsigned ch);  // one count clock
  void tpu_raise(unsigned ch, unsigned flag);
  void tpu_sync(unsigned ch, u64 t);
  void tpu_sync_all(u64 t) { for (unsigned i = 0; i < 6; ++i) tpu_sync(i, t); }
  void tpu_reschedule(unsigned ch);
  void tpu_update_levels(unsigned ch);
  void tpu_reset();
  u8 tpu_read(u32 a);
  void tpu_write(u32 a, u8 v);
  static void on_tpu(void* channel, u64, u64);

  // SCI (section 14)
  struct Sci {
    u8 smr = 0, brr = 0xFF, scr = 0, tdr = 0xFF, ssr = 0x84, rdr = 0, scmr = 0xF2;
    bool shifting = false;
    u8 shift_byte = 0;
    u64 rx_next = 0;
    std::deque<u8> queue;
    u64 transmitted = 0;
    emu::Scheduler::EventId tx_event = 0, rx_event = 0;
    ByteHook tx_hook;
    Exchange peer;
    Peripherals2320* owner = nullptr;
    unsigned index = 0;
  };
  static int sci_channel_of(u32 a) { return a >= 0xFFFF78 && a < 0xFFFF90 ? int((a - 0xFFFF78) >> 3) : -1; }
  u64 sci_char_states(const Sci& s) const;
  u8 sci_read(u32 a);
  void sci_write(u32 a, u8 v);
  void sci_dma_write_tdr(unsigned ch, u8 v);
  u8 sci_dma_read_rdr(unsigned ch);
  void sci_transmitter_off(unsigned ch);
  void sci_kick(unsigned ch, u64 at);  // start what is due, offer the DMAC, update the levels
  void sci_offer_dma(unsigned ch);
  void sci_update_levels(unsigned ch);
  void sci_receive_byte(unsigned ch, u8 byte);
  void sci_schedule_rx(unsigned ch);
  void sci_reset(unsigned ch);
  static void on_sci_tx(void* sci, u64, u64);
  static void on_sci_rx(void* sci, u64, u64);

  // A/D converter (section 16)
  u8 adc_read(u32 a);
  void adc_write(u32 a, u8 v);
  unsigned adc_first() const { return (adcsr_ & 0x10) ? (adcsr_ & 4) : (adcsr_ & 7); }
  unsigned adc_clock_select() const { return ((adcr_ >> 2) & 2) | ((adcsr_ >> 3) & 1); }
  void adc_convert(unsigned ch, u64 end);
  void adc_finish();
  void adc_update_level() { set_level(28, (adcsr_ & 0xC0) == 0xC0); }
  static void on_adc(void* self, u64, u64);

  // ports (section 9)
  u8 port_read(u32 a);
  void port_write(u32 a, u8 v);

  emu::Scheduler& sched_;
  Cpu& cpu_;
  Bus& bus_;
  emu::IoMux& io_;

  FileDevice file_dev_{*this};
  SysDevice sys_dev_{*this};
  DmaDevice dma_dev_{*this};
  TpuDevice tpu_dev_{*this};
  SciDevice sci_dev_{*this};
  AdcDevice adc_dev_{*this};
  PortDevice port_dev_{*this};

  std::array<u8, kRegSize> file_{};

  // interrupt controller and system registers
  std::array<u64, 2> pending_{}, level_{};
  unsigned last_accepted_ = ~0u;
  std::array<u8, 11> ipr_{};
  u8 syscr_ = 0x01, sbycr_ = 0x08;

  // DMAC
  std::array<DmaChannel, 4> dma_{};
  std::array<u8, 4> dmacr_{};
  u8 dmawer_ = 0, dmatcr_ = 0, dmabcrh_ = 0, dmabcrl_ = 0;
  std::array<bool, 4> dma_reported_{}, dma_ended_{};

  // TPU
  std::array<TpuChannel, 6> tpu_{};
  u8 tstr_ = 0, tsyr_ = 0;

  // SCI
  std::array<Sci, kSciCount> sci_{};

  // A/D
  std::array<u16, kAnalogInputs> adc_input_{};
  std::array<u16, 4> addr_{};
  u8 adcsr_ = 0, adcr_ = 0x3F, adc_temp_ = 0;
  unsigned adc_channel_ = 0;
  u16 adc_sample_ = 0;
  u64 adc_end_ = 0;
  emu::Scheduler::EventId adc_event_ = 0;

  // ports
  std::array<u8, kPortCount> pddr_{}, pdr_{};
  PortInput port_input_;
  PortWrite port_write_;
};

}  // namespace h8300h

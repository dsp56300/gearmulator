// SH7014 / SH7042 direct memory access controller (SH7014 hardware manual
// section 9; the SH7040's has four channels and a 24-bit DMATCR).
//
// Channels with registers at H'FFFF86C0 + 16n:
//   +0 SAR     source address (32 bits, counts)
//   +4 DAR     destination address (32 bits, counts)
//   +8 DMATCR  transfer count (bits 15-0; 0 = 65,536; bits 31-16 read 0)
//   +C CHCR    -(31-19) RL AM AL | DM1 DM0 SM1 SM0 RS3 RS2 RS1 RS0 | - DS TM TS1 TS0 IE TE DE
// and the shared DMAOR at H'FFFF86B0:  -(15-3) AE NMIF DME.
// TE, AE and NMIF are cleared by writing 0 after reading 1 (table 9.2 note 1).
//
// Model.  One transfer unit (byte / word / longword per TS) is a read of SAR
// followed by a write of DAR through the Bus (dual address mode, 9.3.4); it
// costs the bus cycles of both accesses (access class of each address), which
// are charged to the CPU with cpu.stall(): the DMAC owns the bus while it
// transfers and nothing else runs.  SAR/DAR advance per SM/DM, DMATCR
// decrements, TE is set when it reaches 0 and DEI0/DEI1 is requested when IE
// is set.  A channel transfers while DE && DME && !TE && !NMIF && !AE (9.3.1).
//
// Requests (9.3.2):
//   * auto-request: enabling the channel schedules the first unit
//     kRequestLatency states later.  Burst mode (TM = 1) then moves the whole
//     block at once; cycle-steal mode moves one unit per event and hands the
//     bus back to the CPU for kCycleStealGap states between units (figure 9.7).
//   * external request (RS = 0, 2, 3): set_dreq() drives the DREQn pin, sampled
//     by low level (DS = 0) or falling edge (DS = 1); same scheduling as the
//     auto-request.  DRAK is pulsed once per accepted sampling (RL polarity),
//     DACK around the read or the write cycle per AM (AL polarity), through
//     the sinks installed with set_dack_sink / set_drak_sink.
//   * on-chip module request (RS >= 6: MTU TGInA, A/D ADI, SCI TXIn/RXIn):
//     while a channel has DE set for the source the Intc routes it here
//     (dma_request) instead of to the CPU.  Each request moves one unit
//     (cycle steal) or the whole remaining block (burst) synchronously, then
//     the module's request flag is cleared through the callback attached for
//     that source (the H8 DTC pattern; 9.3.2 "the request is automatically
//     discontinued").
// Channel priority is fixed at 0 > 1 (9.3.3): one scheduler event serves the
// highest-priority ready channel.
//
// Approximations (all noted again where they apply):
//   * Single address mode (RS = 2, 3) is emulated as a dual transfer: the DACK
//     side is still accessed at SAR/DAR (a host may mirror the device there),
//     its address is not counted and costs no bus cycles, and DACK is pulsed.
//   * A burst is atomic: a DREQ negated or an NMI arriving "during" a burst
//     is only seen after it (9.3.4 burst mode, 9.3.6).
//   * Address errors (table 5.5): word/longword to a misaligned address,
//     longword to the 8-bit peripheral line, or any access outside the two
//     decoded windows.  The offending unit completes and updates SAR/DAR/
//     DMATCR (9.3.6), then AE is set, all channels stop and the CPU takes the
//     DMAC address error exception (vector 10).
//   * Standby mode initialisation of the registers is not modelled; SAR, DAR
//     and DMATCR (undefined at reset) reset to 0.
#pragma once
#include <array>
#include <functional>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/cpu.hpp"
#include "cpu/sh2/intc.hpp"

namespace sh2 {

class Dmac final : public Device, public DmaRequestClient {
 public:
  static constexpr u32 kDmaor = 0xFFFF86B0u;
  static constexpr unsigned kMaxChannels = 4;
  static constexpr u32 kChBase[kMaxChannels] = {0xFFFF86C0u, 0xFFFF86D0u, 0xFFFF86E0u, 0xFFFF86F0u};
  static constexpr u32 kSar = 0x0, kDar = 0x4, kDmatcr = 0x8, kChcr = 0xC;

  // CHCR bits.
  static constexpr u32 kDe = 1u << 0, kTe = 1u << 1, kIe = 1u << 2, kTm = 1u << 5, kDs = 1u << 6;
  static constexpr u32 kAl = 1u << 16, kAm = 1u << 17, kRl = 1u << 18;
  static constexpr unsigned kTsShift = 3, kRsShift = 8, kSmShift = 12, kDmShift = 14;
  static constexpr u32 kChcrMask = 0x0007FF7Fu;  // implemented bits (bits 31-19 and 7 read 0)
  // DMAOR bits.
  static constexpr u16 kDme = 1u << 0, kNmif = 1u << 1, kAe = 1u << 2;

  // Timing (9.3.5): a transfer starts at the earliest three states after the
  // request is sampled; in cycle-steal mode the CPU gets the bus for one bus
  // cycle between units, taken as the shortest one (one state).
  static constexpr u64 kRequestLatency = 3, kCycleStealGap = 1;

  using FlagClear = std::function<void()>;
  using PinSink = std::function<void(bool high)>;

  // channels: 2 (SH7014, 16-bit DMATCR) or 4 (SH7042, 24-bit DMATCR).
  Dmac(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, Bus& bus, Cpu& cpu, unsigned channels = 2);
  ~Dmac() override;

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;
  void write16(u32 addr, u16 value) override;
  u32 read32(u32 addr) override;
  void write32(u32 addr, u32 value) override;

  // --- module side -----------------------------------------------------------
  // Clears the module's request flag after a transfer served its request.
  void attach(IrqSrc src, FlagClear clear) { clear_[size_t(src)] = std::move(clear); }
  void dma_request(IrqSrc src) override;
  // NMI input: sets NMIF and stops every channel (9.2.5; usage note 3).
  void on_nmi();

  // --- pin side ----------------------------------------------------------------
  void set_dreq(unsigned ch, bool low);
  void set_dack_sink(unsigned ch, PinSink s) { dack_[ch & 3] = std::move(s); }
  void set_drak_sink(unsigned ch, PinSink s) { drak_[ch & 3] = std::move(s); }
  unsigned channels() const { return channels_; }

  // --- state ---------------------------------------------------------------------
  u32 sar(unsigned ch) const { return ch_[ch & 3].sar; }
  u32 dar(unsigned ch) const { return ch_[ch & 3].dar; }
  u32 dmatcr(unsigned ch) const { return ch_[ch & 3].tcr; }
  u32 chcr(unsigned ch) const { return ch_[ch & 3].chcr; }
  u16 dmaor() const { return dmaor_; }
  bool enabled(unsigned ch) const;
  u64 transfers() const { return transfers_; }

 private:
  struct Channel {
    u32 sar = 0, dar = 0, tcr = 0, chcr = 0;
    bool te_read = false;    // TE read as 1 since the last write
    bool dreq_low = false;   // DREQn pin level
    bool dreq_edge = false;  // falling edge latched (DS = 1)
  };

  static void on_event(void* self, u64 when, u64 now);
  static bool module_source(u32 chcr, IrqSrc& src);
  static bool external(u32 chcr) { const unsigned rs = (chcr >> kRsShift) & 0xF; return rs == 0 || rs == 2 || rs == 3; }
  static bool burst(u32 chcr) { return (chcr & kTm) != 0; }

  unsigned channel_of(u32 addr) const { return ((addr - kChBase[0]) >> 4) & 3; }
  bool any_ready() const;
  bool ready(unsigned ch) const;  // enabled and holding an auto / external request
  bool address_error(u32 addr, unsigned bytes) const;
  u32 cost(u32 addr, unsigned bytes) const { return bus_.access_class_of(bus_.attr(addr)).cycles(bytes); }
  // One transfer unit; false when the channel stopped (TE set or address error).
  bool transfer_unit(unsigned ch);
  void run_channel(unsigned ch);  // one unit, or the whole block in burst mode
  void service();
  void kick(bool routes = true);
  void update_routes();
  void update_irq(unsigned ch);
  void pulse(const PinSink& s, bool active_low) { if (s) { s(!active_low); s(active_low); } }

  u32 peek32(u32 addr) const;
  void write_reg(u32 addr, u32 value, u32 mask);  // masked write of the longword register at addr
  void write_chcr(unsigned ch, u32 value, u32 mask);
  void write_dmaor(u16 value);

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  Bus& bus_;
  Cpu& cpu_;
  emu::Scheduler::EventId event_ = 0;

  unsigned channels_;
  u32 tcr_mask_;
  std::array<Channel, kMaxChannels> ch_{};
  u16 dmaor_ = 0;
  u16 dmaor_read_ = 0;  // AE / NMIF read as 1 since the last write
  std::array<bool, size_t(IrqSrc::kCount)> routed_{};
  FlagClear clear_[size_t(IrqSrc::kCount)];
  PinSink dack_[kMaxChannels], drak_[kMaxChannels];
  u64 transfers_ = 0;
};

}  // namespace sh2

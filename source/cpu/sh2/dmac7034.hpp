// SH7034 direct memory access controller (SH7032/34 hardware manual section 9).
//
// Four channels with 16-byte register blocks at H'5FFFF40 + 16n:
//   +0 SAR   source address (32 bits, two 16-bit halves)
//   +4 DAR   destination address
//   +A TCR   transfer count (16 bits; 0 = 65,536)
//   +E CHCR  DM1 DM0 SM1 SM0 RS3 RS2 RS1 RS0 | AM AL DS TM TS IE TE DE
// and the shared DMAOR at H'5FFFF48: PR1 PR0 (bits 9-8) | AE NMIF DME.
// TE, AE and NMIF are cleared by writing 0 after reading 1.
//
// Same model as the SH7014 Dmac: a transfer unit (byte or word per TS) is a
// bus read of SAR and a write of DAR, charged to the CPU with stall(); SAR /
// DAR advance per SM / DM, TCR decrements, TE and DEIn follow.  Requests:
// auto-request (RS = C) schedules kRequestLatency states after enabling and
// runs in burst (TM = 1) or cycle-steal mode; DREQ (RS = 0, 2, 3; channels 0
// and 1) through set_dreq(); on-chip module requests (RXIn / TXIn, IMIA0-3,
// ADI) are routed here by the Intc while a channel has DE set for them and
// move one unit (cycle steal) or the block (burst) per request, then clear
// the module's flag through the attached callback.  Priority: PR = 00 fixed
// 0 > 3 > 2 > 1, PR = 01 fixed 1 > 3 > 2 > 0, round robin (1x) as PR = 00.
// Single address mode is approximated as in the SH7014 Dmac (the DACK side
// still addresses SAR / DAR, uncounted and free).
#pragma once
#include <array>
#include <functional>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/cpu.hpp"
#include "cpu/sh2/intc.hpp"

namespace sh2 {

class Dmac7034 final : public Device, public DmaRequestClient {
 public:
  static constexpr unsigned kChannels = 4;
  static constexpr u32 kBase = 0x05FFFF40u, kDmaor = 0x05FFFF48u;
  static constexpr u32 kSar = 0x0, kDar = 0x4, kTcr = 0xA, kChcr = 0xE;
  // CHCR bits.
  static constexpr u16 kDe = 1u << 0, kTe = 1u << 1, kIe = 1u << 2, kTs = 1u << 3, kTm = 1u << 4, kDs = 1u << 5,
                       kAl = 1u << 6, kAm = 1u << 7;
  static constexpr unsigned kRsShift = 8, kSmShift = 12, kDmShift = 14;
  // DMAOR bits.
  static constexpr u16 kDme = 1u << 0, kNmif = 1u << 1, kAe = 1u << 2, kPrMask = 0x0300;
  static constexpr u64 kRequestLatency = 3, kCycleStealGap = 1;

  using FlagClear = std::function<void()>;

  Dmac7034(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, Bus& bus, Cpu& cpu);
  ~Dmac7034() override;

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;
  void write16(u32 addr, u16 value) override;
  u32 read32(u32 addr) override;
  void write32(u32 addr, u32 value) override;

  void attach(IrqSrc src, FlagClear clear) { clear_[size_t(src)] = std::move(clear); }
  void dma_request(IrqSrc src) override;
  void on_nmi();
  void set_dreq(unsigned ch, bool low);

  u32 sar(unsigned ch) const { return ch_[ch & 3].sar; }
  u32 dar(unsigned ch) const { return ch_[ch & 3].dar; }
  u16 tcr(unsigned ch) const { return ch_[ch & 3].tcr; }
  u16 chcr(unsigned ch) const { return ch_[ch & 3].chcr; }
  u16 dmaor() const { return dmaor_; }
  bool enabled(unsigned ch) const;
  u64 transfers() const { return transfers_; }

 private:
  struct Channel {
    u32 sar = 0, dar = 0;
    u16 tcr = 0, chcr = 0;
    bool te_read = false, dreq_low = false, dreq_edge = false;
  };

  static void on_event(void* self, u64 when, u64 now);
  static bool module_source(u16 chcr, IrqSrc& src);
  static bool external(u16 chcr) { const unsigned rs = (chcr >> kRsShift) & 0xF; return rs == 0 || rs == 2 || rs == 3; }
  static bool burst(u16 chcr) { return (chcr & kTm) != 0; }
  const unsigned* order() const;

  bool ready(unsigned ch) const;
  bool address_error(u32 addr, unsigned bytes) const;
  u32 cost(u32 addr, unsigned bytes) const { return bus_.access_class_of(bus_.attr(addr)).cycles(bytes); }
  bool transfer_unit(unsigned ch);
  void run_channel(unsigned ch);
  void service();
  void kick();
  void update_routes();
  void update_irq(unsigned ch);
  void write_chcr(unsigned ch, u16 value);
  void write_dmaor(u16 value);

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  Bus& bus_;
  Cpu& cpu_;
  emu::Scheduler::EventId event_ = 0;
  std::array<Channel, kChannels> ch_{};
  u16 dmaor_ = 0, dmaor_read_ = 0;
  std::array<bool, size_t(IrqSrc::kCount)> routed_{};
  FlagClear clear_[size_t(IrqSrc::kCount)];
  u64 transfers_ = 0;
};

}  // namespace sh2

// SH7014 bus state controller (manual section 8).
#include "cpu/sh2/bsc.hpp"
#include "cpu/sh2/machine.hpp"
#include "common/test_util.hpp"

using namespace sh2;

namespace {

constexpr u32 kIprh = 0xFFFF8356u;
constexpr u32 kCs1Ram = 0x00400000u;

// SH7014 in MCU mode 1: 64 KB of CS0 RAM (vectors, marker), 4 KB of CS1 RAM
// for the timing tests, code in on-chip RAM.  Every interrupt handler writes
// its vector number (& H'7F) to H'F100 through R1 and returns; the main loop
// is  MOV.W @R2,R0 ; BRA loop ; NOP  with R2 pointing into CS1.
struct System {
  Machine m;
  Bsc bsc;
  static constexpr u32 kRam = 0xFFFFF000u;
  static constexpr u32 kMarker = 0xF100;
  System() : m(ChipModel::SH7014, 1), bsc(m.sched(), m.cpu(), m.intc(), m.bus(), m.config()) {
    m.bus().map_ram(0x00000000, 0x10000, Bus::kClsCs0);
    m.bus().map_ram(kCs1Ram, 0x1000, Bus::kClsCs1);
    bsc.map(m.io());
    for (u32 v = 64; v < 160; ++v) {
      const u32 h = 0x1000 + (v - 64) * 16;
      m.bus().write32(v * 4, h);
      const u16 code[] = {u16(0xE000 | (v & 0x7F)), 0x2102, 0x002B, 0x0009};  // MOV #v,R0 ; MOV.L R0,@R1 ; RTE ; NOP
      u32 a = h;
      for (u16 w : code) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    }
    const u16 loop[] = {0x6021, 0xAFFD, 0x0009};  // MOV.W @R2,R0 ; BRA -6 ; slot NOP
    u32 a = kRam;
    for (u16 w : loop) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    m.bus().write32(0, kRam);
    m.bus().write32(4, 0xFFFFFBF0u);
    m.cpu().invalidate_all();
    m.reset();
    bsc.reset();
    m.cpu().regs().sr &= ~Cpu::kIMask;
    m.cpu().regs().r[1] = kMarker;
    m.cpu().regs().r[2] = kCs1Ram + 0x100;
    m.bus().write32(kMarker, 0);
  }
  Bus& bus() { return m.bus(); }
  Intc& intc() { return m.intc(); }
  u32 marker() { return bus().read32(kMarker); }
  u32 wait_vector(u64 max) {
    bus().write32(kMarker, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    return marker();
  }
  // States taken by the MOV.W @R2,R0 at the top of the loop.
  unsigned mov_w_cost() {
    m.cpu().regs().pc = kRam;
    return m.cpu().step();
  }
  // Exact time control between runs (no instruction granularity).
  void advance(u64 states) { m.cpu().stall(states); }
};

void test_reset_values_and_reserved_bits() {
  System s;
  Bus& b = s.bus();
  // Table 8.2 power-on values.
  CHECK_EQ(b.read16(Bsc::kBcr1), 0x200F);
  CHECK_EQ(b.read16(Bsc::kBcr2), 0xFFFF);
  CHECK_EQ(b.read16(Bsc::kWcr1), 0xFFFF);
  CHECK_EQ(b.read16(Bsc::kWcr2), 0x000F);
  CHECK_EQ(b.read16(Bsc::kDcr), 0x0000);
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0000);
  CHECK_EQ(b.read16(Bsc::kRtcnt), 0x0000);
  CHECK_EQ(b.read16(Bsc::kRtcor), 0x0000);
  // BCR1: bits 15-14, 12-9 and 7-4 read 0, bit 13 reads 1 (8.2.1).
  b.write16(Bsc::kBcr1, 0xFFFF);
  CHECK_EQ(b.read16(Bsc::kBcr1), 0x210F);
  b.write16(Bsc::kBcr1, 0x0000);
  CHECK_EQ(b.read16(Bsc::kBcr1), 0x2000);
  CHECK(!s.bsc.multiplex_io());
  b.write16(Bsc::kBcr1, 0x0100);
  CHECK(s.bsc.multiplex_io());
  // BCR2 / WCR1: fully read/write.
  b.write16(Bsc::kBcr2, 0x1234);
  CHECK_EQ(b.read16(Bsc::kBcr2), 0x1234);
  CHECK_EQ(s.bsc.cs_idle(3), 0u);  // IW31-30 = bits 15-14
  CHECK_EQ(s.bsc.cs_idle(2), 1u);  // IW21-20 = bits 13-12
  CHECK_EQ(s.bsc.cs_idle(1), 0u);  // IW11-10 = bits 11-10
  CHECK_EQ(s.bsc.cs_idle(0), 2u);  // IW01-00 = bits 9-8
  CHECK_EQ(s.bsc.cs_continuous_idle(3), false);  // CW3 = bit 7
  CHECK_EQ(s.bsc.cs_continuous_idle(1), true);   // CW1 = bit 5
  CHECK_EQ(s.bsc.cs_continuous_idle(0), true);   // CW0 = bit 4
  CHECK_EQ(s.bsc.cs_assert_extension(2), true);  // SW2 = bit 2
  CHECK_EQ(s.bsc.cs_assert_extension(3), false); // SW3 = bit 3
  b.write16(Bsc::kWcr1, 0x8421);
  CHECK_EQ(b.read16(Bsc::kWcr1), 0x8421);
  CHECK_EQ(s.bsc.cs_wait(3), 8u);
  CHECK_EQ(s.bsc.cs_wait(2), 4u);
  CHECK_EQ(s.bsc.cs_wait(1), 2u);
  CHECK_EQ(s.bsc.cs_wait(0), 1u);
  // WCR2: bits 15-6 reserved (8.2.4).
  b.write16(Bsc::kWcr2, 0xFFFF);
  CHECK_EQ(b.read16(Bsc::kWcr2), 0x003F);
  CHECK_EQ(s.bsc.dma_cs_wait(), 15u);
  CHECK_EQ(s.bsc.dma_dram_wait(), 3u);
  // DCR: bits 6 and 3 reserved (8.2.5).
  b.write16(Bsc::kDcr, 0xFFFF);
  CHECK_EQ(b.read16(Bsc::kDcr), 0xFFB7);
  CHECK_EQ(s.bsc.dram_width(), 16u);
  CHECK_EQ(s.bsc.dram_row_bits(), 12u);
  CHECK_EQ(s.bsc.dram_refresh_ras_cycles(), 5u);
  CHECK(s.bsc.dram_burst() && s.bsc.dram_ras_down() && s.bsc.dram_idle());
  // RTCSR: bits 15-7 reserved, CMF cannot be set by software (8.2.6).
  b.write16(Bsc::kRtcsr, 0xFFFF);
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x003F);
  b.write16(Bsc::kRtcsr, 0x0000);
  // RTCNT / RTCOR: 8-bit, upper byte reads 0 (8.2.7, 8.2.8).
  b.write16(Bsc::kRtcnt, 0x01FF);
  CHECK_EQ(b.read16(Bsc::kRtcnt), 0x00FF);
  b.write16(Bsc::kRtcor, 0xAB12);
  CHECK_EQ(b.read16(Bsc::kRtcor), 0x0012);
  // Byte and longword accesses (table 8.2: 8/16/32).
  b.write16(Bsc::kBcr1, 0x200F);
  CHECK_EQ(b.read8(Bsc::kBcr1), 0x20);
  CHECK_EQ(b.read8(Bsc::kBcr1 + 1), 0x0F);
  b.write8(Bsc::kBcr1 + 1, 0x05);
  CHECK_EQ(b.read16(Bsc::kBcr1), 0x2005);
  b.write8(Bsc::kBcr1, 0x01);  // IOE
  CHECK_EQ(b.read16(Bsc::kBcr1), 0x2105);
  b.write16(Bsc::kBcr2, 0xFFFF);
  CHECK_EQ(b.read32(Bsc::kBcr1), 0x2105FFFFu);
  b.write32(Bsc::kWcr1, 0x12340025u);
  CHECK_EQ(b.read16(Bsc::kWcr1), 0x1234);
  CHECK_EQ(b.read16(Bsc::kWcr2), 0x0025);
  // Not ours: the flash RAMER bytes between WCR2 and DCR read as unassigned.
  CHECK_EQ(s.m.io().owner(0xFFFF8628u), nullptr);
  CHECK_EQ(s.m.io().owner(0xFFFF8629u), nullptr);
  // reset() restores everything.
  s.bsc.reset();
  CHECK_EQ(b.read16(Bsc::kBcr1), 0x200F);
  CHECK_EQ(b.read16(Bsc::kBcr2), 0xFFFF);
  CHECK_EQ(b.read16(Bsc::kWcr1), 0xFFFF);
  CHECK_EQ(b.read16(Bsc::kWcr2), 0x000F);
  CHECK_EQ(b.read16(Bsc::kDcr), 0x0000);
  CHECK_EQ(b.read16(Bsc::kRtcnt), 0x0000);
  CHECK_EQ(b.read16(Bsc::kRtcor), 0x0000);
}

void test_bus_width_and_waits_retime_accesses() {
  System s;
  Bus& b = s.bus();
  // Power-on: CS1 is 16-bit with 15 waits -> a word read costs 1 + 15 states.
  const AccessClass& cs1 = b.access_class(Bus::kClsCs1);
  CHECK_EQ(cs1.width, 16u);
  CHECK_EQ(cs1.wait, 15u);
  CHECK_EQ(cs1.base, Bsc::kOrdinaryBase);
  CHECK(cs1.external && cs1.cacheable);
  CHECK_EQ(s.mov_w_cost(), 17u);
  // WCR1: no waits in CS1 -> one state.
  b.write16(Bsc::kWcr1, 0xFF0F);
  CHECK_EQ(b.access_class(Bus::kClsCs1).wait, 0u);
  CHECK_EQ(s.mov_w_cost(), 2u);
  // BCR1: CS1 8-bit -> a word read takes two bus cycles.
  b.write16(Bsc::kBcr1, 0x200D);
  CHECK_EQ(b.access_class(Bus::kClsCs1).width, 8u);
  CHECK_EQ(s.bsc.cs_width(1), 8u);
  CHECK_EQ(s.mov_w_cost(), 4u);
  // 8-bit with 3 waits: 2 x (1 + 3).
  b.write16(Bsc::kWcr1, 0xFF3F);
  CHECK_EQ(s.mov_w_cost(), 10u);
  // Back to 16-bit, 3 waits: 1 x (1 + 3).
  b.write16(Bsc::kBcr1, 0x200F);
  CHECK_EQ(s.mov_w_cost(), 5u);
  // The other areas follow their own nibbles / bits.
  b.write16(Bsc::kWcr1, 0x5A3F);
  b.write16(Bsc::kBcr1, 0x2004);  // only A2SZ set
  CHECK_EQ(b.access_class(Bus::kClsCs0).wait, 15u);
  CHECK_EQ(b.access_class(Bus::kClsCs1).wait, 3u);
  CHECK_EQ(b.access_class(Bus::kClsCs2).wait, 10u);
  CHECK_EQ(b.access_class(Bus::kClsCs3).wait, 5u);
  CHECK_EQ(b.access_class(Bus::kClsCs2).width, 16u);
  CHECK_EQ(b.access_class(Bus::kClsCs3).width, 8u);
  CHECK_EQ(b.access_class(Bus::kClsCs1).width, 8u);
  // CS0 in an on-chip-ROM-disabled mode is sized by the mode pin, not A0SZ
  // (8.2.1 note): mode 1 = 16-bit even with A0SZ = 0.
  CHECK_EQ(b.access_class(Bus::kClsCs0).width, 16u);
  CHECK_EQ(s.bsc.cs_width(0), 16u);
  {
    Machine m0(ChipModel::SH7014, 0);
    Bsc bsc0(m0.sched(), m0.cpu(), m0.intc(), m0.bus(), m0.config());
    CHECK_EQ(bsc0.cs_width(0), 8u);
    CHECK_EQ(m0.bus().access_class(Bus::kClsCs0).width, 8u);
    bsc0.map(m0.io());
    m0.bus().write16(Bsc::kBcr1, 0x200F);
    CHECK_EQ(m0.bus().access_class(Bus::kClsCs0).width, 8u);  // A0SZ ignored
  }
  {
    // On-chip ROM enabled (SH7017, mode 2): A0SZ governs.
    Machine m2(ChipModel::SH7017, 2);
    Bsc bsc2(m2.sched(), m2.cpu(), m2.intc(), m2.bus(), m2.config());
    bsc2.map(m2.io());
    CHECK_EQ(m2.bus().access_class(Bus::kClsCs0).width, 16u);
    m2.bus().write16(Bsc::kBcr1, 0x200E);
    CHECK_EQ(m2.bus().access_class(Bus::kClsCs0).width, 8u);
    CHECK_EQ(bsc2.cs_width(0), 8u);
  }
  // Multiplexed I/O on CS3 adds the fixed address-output cycles (8.5.1).
  b.write16(Bsc::kBcr1, 0x2104);
  CHECK_EQ(b.access_class(Bus::kClsCs3).wait, unsigned(5 + Bsc::kMuxIoAddressCycles));
  // DRAM: 8-bit no-wait by default = Tp Tr Tc1 Tc2 (figure 8.7).
  const AccessClass& dram0 = b.access_class(Bus::kClsDram);
  CHECK_EQ(dram0.width, 8u);
  CHECK_EQ(dram0.wait, unsigned(Bsc::kDramOverhead));
  CHECK(dram0.external && dram0.cacheable);
  // SZ0 = 16-bit, TPC, DWR = 2 waits -> overhead + 1 + 2.
  b.write16(Bsc::kDcr, 0x8204);
  const AccessClass& dram1 = b.access_class(Bus::kClsDram);
  CHECK_EQ(dram1.width, 16u);
  CHECK_EQ(dram1.wait, unsigned(Bsc::kDramOverhead + 3));
  CHECK_EQ(s.bsc.dram_read_wait(), 2u);
  CHECK_EQ(s.bsc.dram_write_wait(), 0u);
  CHECK_EQ(s.bsc.dram_ras_precharge(), 2u);
  // RCD adds a row-address cycle.
  b.write16(Bsc::kDcr, 0x4000);
  CHECK_EQ(b.access_class(Bus::kClsDram).wait, unsigned(Bsc::kDramOverhead + 1));
  // WCR2 does not retime CPU accesses (DMA single-address waits only).
  b.write16(Bsc::kWcr2, 0x0000);
  CHECK_EQ(b.access_class(Bus::kClsCs1).wait, 3u);
  CHECK_EQ(b.access_class(Bus::kClsDram).wait, unsigned(Bsc::kDramOverhead + 1));
  // reset() reinstalls the power-on timing.
  s.bsc.reset();
  CHECK_EQ(b.access_class(Bus::kClsCs1).width, 16u);
  CHECK_EQ(b.access_class(Bus::kClsCs1).wait, 15u);
  CHECK_EQ(b.access_class(Bus::kClsCs3).wait, 15u);
  CHECK_EQ(b.access_class(Bus::kClsDram).width, 8u);
  CHECK_EQ(b.access_class(Bus::kClsDram).wait, unsigned(Bsc::kDramOverhead));
  CHECK_EQ(s.mov_w_cost(), 17u);
}

void test_refresh_timer_counts_and_matches() {
  System s;
  Bus& b = s.bus();
  // Stopped after reset: RTCNT = RTCOR = 0 never sets CMF (8.2.6 note).
  s.advance(5000);
  CHECK_EQ(b.read16(Bsc::kRtcnt), 0x0000);
  CHECK_EQ(b.read16(Bsc::kRtcsr) & Bsc::kCmf, 0);
  // phi/2, RTCOR = 10: the counter reaches 10 after 20 states and matches
  // on the next count-up (2 states later): RTCNT clears, CMF set.
  b.write16(Bsc::kRtcor, 10);
  b.write16(Bsc::kRtcsr, 0x0004);
  CHECK_EQ(s.bsc.refresh_clock_shift(), 1u);
  s.advance(20);
  CHECK_EQ(b.read16(Bsc::kRtcnt), 10);
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0004);
  s.advance(2);
  CHECK_EQ(b.read16(Bsc::kRtcnt), 0);
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0044);
  // No interrupt without CMIE.
  CHECK_EQ(s.intc().request(IrqSrc::Cmi), false);
  // Keeps counting with period RTCOR + 1 ticks: after 7 more ticks RTCNT = 7.
  s.advance(14);
  CHECK_EQ(b.read16(Bsc::kRtcnt), 7);
  // A long gap wraps any number of times: 1000 ticks from here -> (7 + 1000) mod 11.
  s.advance(2000);
  CHECK_EQ(b.read16(Bsc::kRtcnt), (7 + 1000) % 11);
  // Other prescalers (8.2.6): phi/8, phi/32, phi/128, phi/512, phi/2048, phi/4096.
  static const struct { u16 cks; unsigned shift; } kRates[] = {
      {1, 1}, {2, 3}, {3, 5}, {4, 7}, {5, 9}, {6, 11}, {7, 12}};
  for (const auto& r : kRates) {
    b.write16(Bsc::kRtcsr, 0x0000);  // stop
    b.write16(Bsc::kRtcor, 0xFF);
    b.write16(Bsc::kRtcnt, 0);
    b.write16(Bsc::kRtcsr, u16(r.cks << 2));
    CHECK_EQ(s.bsc.refresh_clock_shift(), r.shift);
    // Align to the tick boundary first: advance to the next multiple of the period.
    const u64 period = u64(1) << r.shift;
    s.advance(period - (s.m.now() & (period - 1)));
    const u16 c0 = b.read16(Bsc::kRtcnt);
    s.advance(period * 5);
    CHECK_EQ(b.read16(Bsc::kRtcnt), u16(c0 + 5));
    s.advance(period - 1);
    CHECK_EQ(b.read16(Bsc::kRtcnt), u16(c0 + 5));
    s.advance(1);
    CHECK_EQ(b.read16(Bsc::kRtcnt), u16(c0 + 6));
  }
  // Writing RTCNT / RTCOR while running.
  b.write16(Bsc::kRtcsr, 0x0004);  // phi/2
  b.write16(Bsc::kRtcor, 3);
  b.write16(Bsc::kRtcnt, 2);
  b.write16(Bsc::kRtcsr, 0x0004);  // (CMF: not read yet, stays set)
  s.advance(2);
  CHECK_EQ(b.read16(Bsc::kRtcnt), 3);
  s.advance(2);
  CHECK_EQ(b.read16(Bsc::kRtcnt), 0);
}

void test_cmf_clears_by_read_then_write_zero() {
  System s;
  Bus& b = s.bus();
  b.write16(Bsc::kRtcor, 1);
  b.write16(Bsc::kRtcsr, 0x0004);  // phi/2
  s.advance(4);                    // 2 ticks: match
  // Writing 0 without having read CMF as 1 does not clear it.
  b.write16(Bsc::kRtcsr, 0x0000);  // also stops the clock
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0040);
  // Read as 1 above; writing 1 keeps it, writing 0 clears it.
  b.write16(Bsc::kRtcsr, 0x0040);
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0040);
  b.write16(Bsc::kRtcsr, 0x0000);
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0000);
  // Same through byte accesses: CMF is in the low byte.
  b.write16(Bsc::kRtcsr, 0x0004);
  s.advance(4);
  b.write16(Bsc::kRtcsr, 0x0000);
  CHECK_EQ(b.read8(Bsc::kRtcsr), 0x00);       // high byte read does not count as reading CMF
  b.write8(Bsc::kRtcsr + 1, 0x00);
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0040);    // still set
  CHECK_EQ(b.read8(Bsc::kRtcsr + 1), 0x40);   // now read as 1
  b.write8(Bsc::kRtcsr, 0x00);                // high-byte write rewrites CMF as 1: no change
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0040);
  b.write8(Bsc::kRtcsr + 1, 0x00);
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0000);
}

void test_cmi_interrupt() {
  System s;
  Bus& b = s.bus();
  b.write16(Bsc::kWcr1, 0x0000);  // fast CS0 so the handler's marker write is quick
  b.write16(kIprh, 0xF000);       // WDT / BSC priority level 15 (IPRH bits 15-12)
  CHECK_EQ(s.intc().level_of(IrqSrc::Cmi), 15u);
  CHECK_EQ(s.intc().vector_of(IrqSrc::Cmi), 153u);
  // RTCOR = 5, phi/2, CMIE: the match is at tick 6 from the CKS write.
  b.write16(Bsc::kRtcor, 5);
  const u64 t0 = s.m.now();
  b.write16(Bsc::kRtcsr, 0x0024);
  const u64 match = ((t0 >> 1) + 6) << 1;
  CHECK_EQ(s.m.sched().next_time(), match);  // one event, at the match
  // The request is raised by the scheduled event exactly at the match time
  // (drive the scheduler by hand to avoid instruction granularity).
  s.advance(match - 1 - s.m.now());
  s.m.sched().run_due(s.m.now());
  CHECK_EQ(s.intc().request(IrqSrc::Cmi), false);
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0024);
  s.advance(1);
  s.m.sched().run_due(s.m.now());
  CHECK_EQ(s.intc().request(IrqSrc::Cmi), true);
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0064);
  CHECK_EQ(b.read16(Bsc::kRtcnt), 0);
  // Vector 153 is taken.
  CHECK_EQ(s.wait_vector(200), 153u & 0x7F);
  CHECK_EQ(s.m.cpu().interrupt_mask(), 15u);
  // Level-sensitive: still requested after RTE while CMF stands, so taken again.
  CHECK_EQ(s.wait_vector(200), 153u & 0x7F);
  // Stop the clock and clear CMF (read 1, write 0): the request drops.
  CHECK_EQ(b.read16(Bsc::kRtcsr) & Bsc::kCmf, unsigned(Bsc::kCmf));
  b.write16(Bsc::kRtcsr, 0x0020);
  CHECK_EQ(s.intc().request(IrqSrc::Cmi), false);
  CHECK_EQ(s.m.sched().next_time(), emu::Scheduler::kNever);  // stopped: no event
  CHECK_EQ(s.wait_vector(200), 0u);
  // Match with CMIE clear sets CMF silently; enabling CMIE afterwards requests at once.
  b.write16(Bsc::kRtcsr, 0x0004);
  CHECK_EQ(s.m.sched().next_time(), emu::Scheduler::kNever);  // no interrupt-capable match: no event
  s.advance(12);
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0044);
  CHECK_EQ(s.intc().request(IrqSrc::Cmi), false);
  b.write16(Bsc::kRtcsr, 0x0064);  // CMIE (writing CMF = 1 keeps it)
  CHECK_EQ(s.intc().request(IrqSrc::Cmi), true);
  CHECK_EQ(s.wait_vector(200), 153u & 0x7F);
  // Masked by IPRH = 0: no interrupt even though requested.
  b.write16(kIprh, 0x0000);
  CHECK_EQ(s.wait_vector(200), 0u);
  CHECK_EQ(s.intc().request(IrqSrc::Cmi), true);
  // Reprogramming RTCOR moves the event.
  b.write16(kIprh, 0xF000);
  b.write16(Bsc::kRtcsr, 0x0020);  // clear CMF (read above), stop
  b.write16(Bsc::kRtcnt, 0);
  b.write16(Bsc::kRtcor, 0x20);
  const u64 t1 = s.m.now();
  b.write16(Bsc::kRtcsr, 0x0028);  // phi/8, CMIE
  CHECK_EQ(s.m.sched().next_time(), ((t1 >> 3) + 0x21) << 3);
  b.write16(Bsc::kRtcor, 0x10);
  CHECK_EQ(s.m.sched().next_time(), ((t1 >> 3) + 0x11) << 3);
  // The stale event was cancelled: stopping the clock leaves nothing to fire.
  b.write16(Bsc::kRtcsr, 0x0020);
  CHECK_EQ(s.m.sched().next_time(), emu::Scheduler::kNever);
}

void test_refresh_requests_counted_only() {
  System s;
  Bus& b = s.bus();
  // RFSH = 1, RMD = 0: a CAS-before-RAS request per match (not executed, only counted).
  b.write16(Bsc::kRtcor, 3);
  b.write16(Bsc::kRtcsr, 0x0006);  // phi/2, RFSH
  CHECK(s.bsc.refresh_enabled() && !s.bsc.self_refresh());
  s.advance(2 * 4 * 5);  // 20 ticks = matches at ticks 4, 8, 12, 16, 20
  CHECK_EQ(s.bsc.refresh_requests(), 5u);
  // Self-refresh: interval timer requests are ignored (8.2.6 RMD).
  b.write16(Bsc::kRtcsr, 0x0007);
  CHECK(s.bsc.self_refresh());
  s.advance(2 * 4 * 5);
  CHECK_EQ(s.bsc.refresh_requests(), 5u);
  // Refresh control off: no requests.
  b.write16(Bsc::kRtcsr, 0x0004);
  s.advance(2 * 4 * 5);
  CHECK_EQ(s.bsc.refresh_requests(), 5u);
  // Reset clears the count and stops the timer.
  s.bsc.reset();
  CHECK_EQ(s.bsc.refresh_requests(), 0u);
  CHECK_EQ(b.read16(Bsc::kRtcsr), 0x0000);
}

}  // namespace

int main() {
  test_reset_values_and_reserved_bits();
  test_bus_width_and_waits_retime_accesses();
  test_refresh_timer_counts_and_matches();
  test_cmf_clears_by_read_then_write_zero();
  test_cmi_interrupt();
  test_refresh_requests_counted_only();
  return test::finish("test_sh2_bsc");
}

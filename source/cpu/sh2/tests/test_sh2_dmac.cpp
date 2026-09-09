// SH7014 direct memory access controller (manual section 9).
#include "cpu/sh2/dmac.hpp"
#include "cpu/sh2/machine.hpp"
#include "common/test_util.hpp"

using namespace sh2;

namespace {

constexpr u32 kDmaor = 0xFFFF86B0u;
constexpr u32 kSar0 = 0xFFFF86C0u, kDar0 = 0xFFFF86C4u, kTcr0 = 0xFFFF86C8u, kChcr0 = 0xFFFF86CCu;
constexpr u32 kSar1 = 0xFFFF86D0u, kDar1 = 0xFFFF86D4u, kTcr1 = 0xFFFF86D8u, kChcr1 = 0xFFFF86DCu;
constexpr u32 kIprc = 0xFFFF834Cu, kIprf = 0xFFFF8352u;

// CHCR fields.
constexpr u32 kDmInc = 1u << 14, kDmDec = 2u << 14, kSmInc = 1u << 12, kSmDec = 2u << 12;
constexpr u32 kRsExtDual = 0u << 8, kRsAuto = 4u << 8, kRsRxi0 = 0xDu << 8;
constexpr u32 kDs = 0x40, kTm = 0x20, kTsWord = 0x08, kTsLong = 0x10, kIe = 0x04, kTe = 0x02, kDe = 0x01;

struct System {
  Machine m;
  Dmac dmac;
  static constexpr u32 kRam = 0xFFFFF000u;
  static constexpr u32 kSrc = 0x2000, kDst = 0x3000;
  System() : m(ChipModel::SH7014, 1), dmac(m.sched(), m.cpu(), m.intc(), m.bus(), m.cpu()) {
    m.bus().map_ram(0x00000000, 0x10000, Bus::kClsCs0);
    dmac.map(m.io());
    // Vectors: every interrupt handler writes its vector number to H'F100 and returns.
    for (u32 v = 64; v < 160; ++v) {
      const u32 h = 0x1000 + (v - 64) * 16;
      m.bus().write32(v * 4, h);
      const u16 code[] = {u16(0xE000 | (v & 0x7F)), 0x2102, 0x002B, 0x0009};  // MOV #v,R0 ; MOV.L R0,@R1 ; RTE ; NOP
      u32 a = h;
      for (u16 w : code) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    }
    // DMAC address error (vector 10): same handler shape at H'4000.
    {
      const u16 code[] = {0xE00A, 0x2102, 0x002B, 0x0009};
      u32 a = 0x4000;
      for (u16 w : code) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
      m.bus().write32(10 * 4, 0x4000);
    }
    // Main program: NOP loop at on-chip RAM.
    const u16 loop[] = {0x0009, 0xAFFD, 0x0009};  // NOP ; BRA -6 ; slot NOP
    u32 a = kRam;
    for (u16 w : loop) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    m.bus().write32(0, kRam);
    m.bus().write32(4, 0xFFFFFBF0u);
    m.cpu().invalidate_all();
    m.reset();
    m.bus().write16(0xFFFF8624, 0x0000);  // WCR1: zero wait states (reset gives 15 to every area)
    dmac.reset();
    m.cpu().regs().sr &= ~Cpu::kIMask;
    m.cpu().regs().r[1] = 0xF100;
    m.bus().write32(0xF100, 0);
    // Source block: 16 distinct longwords.
    for (u32 i = 0; i < 16; ++i) m.bus().write32(kSrc + i * 4, 0x11110000u + i * 0x01010101u);
  }
  Bus& bus() { return m.bus(); }
  Intc& intc() { return m.intc(); }
  u32 marker() { return bus().read32(0xF100); }
  u32 wait_vector(u64 max) {
    bus().write32(0xF100, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    return marker();
  }
  // Run until TE of channel 0 is set (or `max` states elapse); returns the states elapsed.
  u64 run_until_te(u64 max) {
    const u64 t0 = m.now();
    while (!(dmac.chcr(0) & kTe) && m.now() - t0 < max) m.run(1);
    return m.now() - t0;
  }
  bool block_copied(u32 n = 16) {
    for (u32 i = 0; i < n; ++i)
      if (bus().read32(kDst + i * 4) != bus().read32(kSrc + i * 4)) return false;
    return true;
  }
};

void test_reset_and_registers() {
  System s;
  Bus& b = s.bus();
  // Reset values (table 9.2): CHCR and DMAOR 0; the others undefined (0 here).
  CHECK_EQ(b.read32(kChcr0), 0u);
  CHECK_EQ(b.read32(kChcr1), 0u);
  CHECK_EQ(b.read16(kDmaor), 0u);
  CHECK_EQ(b.read32(kTcr0), 0u);
  // SAR / DAR: full 32 bits, 16-bit halves held independently.
  b.write32(kSar0, 0x12345678u);
  CHECK_EQ(b.read32(kSar0), 0x12345678u);
  b.write16(kSar0 + 2, 0xABCD);
  CHECK_EQ(b.read32(kSar0), 0x1234ABCDu);
  b.write16(kSar0, 0x5555);
  CHECK_EQ(b.read32(kSar0), 0x5555ABCDu);
  CHECK_EQ(b.read16(kSar0 + 2), 0xABCDu);
  b.write32(kDar1, 0xFFFFF000u);
  CHECK_EQ(b.read32(kDar1), 0xFFFFF000u);
  CHECK_EQ(b.read8(kDar1 + 1), 0xFFu);
  CHECK_EQ(b.read8(kDar1 + 3), 0x00u);
  // DMATCR: 16 bits; the upper half writes are invalid and read 0 (9.2.3).
  b.write32(kTcr0, 0xFFFFFFFFu);
  CHECK_EQ(b.read32(kTcr0), 0x0000FFFFu);
  b.write16(kTcr1 + 2, 0x0040);
  CHECK_EQ(b.read32(kTcr1), 0x40u);
  CHECK_EQ(b.read16(kTcr1), 0u);
  // CHCR: bits 31-19 and 7 read 0; TE cannot be set by software.  DME is
  // clear so nothing starts.
  b.write32(kChcr1, 0xFFFFFFFFu);
  CHECK_EQ(b.read32(kChcr1), 0x0007FF7Du);
  b.write32(kChcr1, 0);
  CHECK_EQ(b.read32(kChcr1), 0u);
  // DMAOR: bits 15-3 read 0; AE / NMIF cannot be set by software.
  b.write16(kDmaor, 0xFFFF);
  CHECK_EQ(b.read16(kDmaor), 0x0001u);
  b.write16(kDmaor, 0);
  CHECK_EQ(b.read16(kDmaor), 0u);
  CHECK_EQ(s.dmac.transfers(), 0u);
}

void test_burst_auto_request() {
  System s;
  Bus& b = s.bus();
  b.write16(kIprc, 0x8000);  // DEI0 level 8
  b.write32(kSar0, System::kSrc);
  b.write32(kDar0, System::kDst);
  b.write32(kTcr0, 16);
  b.write32(kChcr0, kDmInc | kSmInc | kRsAuto | kTm | kTsLong | kIe | kDe);
  CHECK_EQ(s.dmac.transfers(), 0u);  // DME still 0
  const u64 t0 = s.m.now();
  const u64 insn0 = s.m.cpu().instructions_executed();
  b.write16(kDmaor, 0x0001);
  CHECK_EQ(s.dmac.transfers(), 0u);  // starts kRequestLatency states later
  s.m.run(Dmac::kRequestLatency + 2);  // run() stops at an event due exactly at its end: overshoot
  // The whole block moved at once: 16 units x (2 + 2 states in 16-bit CS0 space).
  CHECK_EQ(s.dmac.transfers(), 16u);
  CHECK(s.block_copied());
  CHECK_EQ(b.read32(kTcr0), 0u);
  CHECK_EQ(b.read32(kSar0), System::kSrc + 64);
  CHECK_EQ(b.read32(kDar0), System::kDst + 64);
  CHECK((b.read32(kChcr0) & kTe) != 0);
  const u64 elapsed = s.m.now() - t0;
  CHECK(elapsed >= Dmac::kRequestLatency + 16 * 8);
  // Machine::run overshoots by up to one CPU slice before and after the event
  // (the NOP/BRA loop is 3 instructions; with 2-state external fetch lines a
  // slice is up to 8 states).
  CHECK(elapsed <= Dmac::kRequestLatency + 16 * 8 + 24);
  // The CPU was stalled for the block: only the slices around the event ran.
  CHECK(s.m.cpu().instructions_executed() - insn0 <= 8);
  // DEI0 (vector 72) with IE set.
  CHECK_EQ(s.wait_vector(100), 72u);
  // TE: read 1 then write 0 clears it and withdraws the interrupt.  A write
  // without the read, or a write of 1, leaves it set.
  const u32 chcr = b.read32(kChcr0);
  b.write32(kChcr0, chcr | kTe);  // 1 written: no change (te_read consumed)
  b.write32(kChcr0, chcr & ~kTe & ~kDe);  // not read since: still set
  CHECK((b.read32(kChcr0) & kTe) != 0);
  CHECK_EQ(s.wait_vector(50), 72u);  // still requesting
  b.write32(kChcr0, b.read32(kChcr0) & ~kTe & ~kDe);
  CHECK_EQ(b.read32(kChcr0) & kTe, 0u);
  CHECK_EQ(s.wait_vector(50), 0u);
  // Decrementing addresses, word size, channel 1 without interrupt.
  b.write32(kSar1, System::kSrc + 62);
  b.write32(kDar1, System::kDst + 0x100 + 62);
  b.write32(kTcr1, 32);
  b.write32(kChcr1, kDmDec | kSmDec | kRsAuto | kTm | kTsWord | kDe);
  s.m.run(Dmac::kRequestLatency + 2);
  CHECK((b.read32(kChcr1) & kTe) != 0);
  CHECK_EQ(b.read32(kSar1), System::kSrc - 2);
  CHECK_EQ(b.read32(kDar1), System::kDst + 0x100 - 2);
  bool ok = true;
  for (u32 i = 0; i < 16; ++i) ok = ok && b.read32(System::kDst + 0x100 + i * 4) == b.read32(System::kSrc + i * 4);
  CHECK(ok);
  CHECK_EQ(s.wait_vector(50), 0u);  // IE = 0
}

void test_cycle_steal_auto_request() {
  System s;
  Bus& b = s.bus();
  b.write32(kSar0, System::kSrc);
  b.write32(kDar0, System::kDst);
  b.write32(kTcr0, 16);
  b.write16(kDmaor, 0x0001);
  const u64 insn0 = s.m.cpu().instructions_executed();
  b.write32(kChcr0, kDmInc | kSmInc | kRsAuto | kTsLong | kDe);
  // One unit per activation, the CPU runs in between.
  s.m.run(Dmac::kRequestLatency + 2);
  CHECK(s.dmac.transfers() >= 1 && s.dmac.transfers() <= 2);
  CHECK_EQ(b.read32(System::kDst), b.read32(System::kSrc));
  const u64 n1 = s.dmac.transfers();
  const u64 elapsed = s.run_until_te(1000);
  CHECK_EQ(s.dmac.transfers(), 16u);
  CHECK(s.block_copied());
  CHECK_EQ(b.read32(kTcr0), 0u);
  // Each remaining unit stalls 8 states (two 2-cycle longword accesses in
  // 16-bit CS0) and leaves the bus for kCycleStealGap.
  CHECK(elapsed >= (16 - n1) * (8 + Dmac::kCycleStealGap) - 1);
  CHECK(elapsed <= (16 - n1) * (8 + Dmac::kCycleStealGap + 5));  // + run(1) granularity
  // The CPU executed at least one instruction per gap.
  CHECK(s.m.cpu().instructions_executed() - insn0 >= 16);
  // Nothing more happens once TE is set, even with DE and DME still 1.
  const u64 n = s.dmac.transfers();
  s.m.run(100);
  CHECK_EQ(s.dmac.transfers(), n);
}

void test_module_request() {
  System s;
  Bus& b = s.bus();
  b.write16(kIprf, 0x0070);  // SCI0 level 7: visible to the CPU only when not routed
  int clears = 0;
  s.dmac.attach(IrqSrc::Rxi0, [&] { ++clears; s.intc().set_request(IrqSrc::Rxi0, false); });
  // Manual 9.4.1 shape: fixed byte source, incrementing destination, RXI0 request, cycle steal.
  b.write8(System::kSrc, 0xA5);
  b.write32(kSar0, System::kSrc);
  b.write32(kDar0, System::kDst);
  b.write32(kTcr0, 3);
  b.write16(kDmaor, 0x0001);
  b.write32(kChcr0, kDmInc | kRsRxi0 | kDe);
  s.m.run(20);
  CHECK_EQ(s.dmac.transfers(), 0u);  // waits for the module
  const u64 t0 = s.m.now();
  s.intc().set_request(IrqSrc::Rxi0, true);
  CHECK_EQ(s.dmac.transfers(), 1u);  // served synchronously
  CHECK_EQ(clears, 1);
  CHECK_EQ(s.intc().request(IrqSrc::Rxi0), false);
  CHECK_EQ(b.read8(System::kDst), 0xA5u);
  CHECK_EQ(b.read32(kTcr0), 2u);
  CHECK_EQ(b.read32(kDar0), System::kDst + 1);
  CHECK_EQ(s.m.now() - t0, 4u);  // stalled for the read and the write (2 states each in CS0)
  CHECK_EQ(s.wait_vector(50), 0u);    // no RXI0 to the CPU
  b.write8(System::kSrc, 0x5A);
  s.intc().set_request(IrqSrc::Rxi0, true);
  b.write8(System::kSrc, 0xC3);
  s.intc().set_request(IrqSrc::Rxi0, true);
  CHECK_EQ(s.dmac.transfers(), 3u);
  CHECK_EQ(clears, 3);
  CHECK_EQ(b.read8(System::kDst + 1), 0x5Au);
  CHECK_EQ(b.read8(System::kDst + 2), 0xC3u);
  CHECK((b.read32(kChcr0) & kTe) != 0);
  // TE set: a further request is not served (flag not cleared) and, the
  // channel still having DE set, not presented to the CPU either.
  s.intc().set_request(IrqSrc::Rxi0, true);
  CHECK_EQ(s.dmac.transfers(), 3u);
  CHECK_EQ(clears, 3);
  CHECK_EQ(s.wait_vector(50), 0u);
  // Clearing DE unroutes the source: the CPU takes RXI0 (vector 129).
  b.write32(kChcr0, b.read32(kChcr0) & ~kDe);
  CHECK_EQ(s.wait_vector(50), 129u & 0x7F);
  s.intc().set_request(IrqSrc::Rxi0, false);
  // A request already pending when the channel is (re)enabled is served then.
  s.intc().set_request(IrqSrc::Rxi0, true);
  b.write32(kChcr0, (b.read32(kChcr0) & ~kTe) | kDe);  // TE read above: cleared; DE set
  CHECK_EQ(s.dmac.transfers(), 4u);
  CHECK_EQ(clears, 4);
  // Burst with a module request: the whole remaining block on one request.
  b.write32(kChcr0, b.read32(kChcr0) & ~kDe);
  b.write32(kTcr0, 5);
  b.write32(kChcr0, kDmInc | kRsRxi0 | kTm | kDe);
  s.intc().set_request(IrqSrc::Rxi0, true);
  CHECK_EQ(s.dmac.transfers(), 9u);
  CHECK_EQ(clears, 5);
  CHECK((b.read32(kChcr0) & kTe) != 0);
}

void test_address_error() {
  System s;
  Bus& b = s.bus();
  b.write32(kSar0, System::kSrc + 1);  // odd word source
  b.write32(kDar0, System::kDst);
  b.write32(kTcr0, 8);
  b.write32(kChcr0, kDmInc | kSmInc | kRsAuto | kTm | kTsWord | kIe | kDe);
  b.write16(kDmaor, 0x0001);
  CHECK_EQ(s.wait_vector(50), 10u);  // DMAC address error
  CHECK_EQ(s.dmac.dmaor() & 0x0004, 0x0004u);  // AE (accessor: no read side effect)
  CHECK_EQ(b.read32(kChcr0) & kTe, 0u);           // TE not set
  CHECK_EQ(b.read32(kTcr0), 7u);                  // the unit completed (9.3.6)
  CHECK_EQ(b.read32(kSar0), System::kSrc + 3);
  CHECK_EQ(s.dmac.transfers(), 1u);
  s.m.run(100);
  CHECK_EQ(s.dmac.transfers(), 1u);  // halted
  CHECK_EQ(s.dmac.enabled(0), false);
  // AE clears by read-then-write-0 only; with DE cleared first nothing resumes.
  b.write16(kDmaor, 0x0001);  // not read yet: stays set
  CHECK_EQ(b.read16(kDmaor) & 0x0004, 0x0004u);
  b.write32(kChcr0, b.read32(kChcr0) & ~kDe);
  b.write16(kDmaor, 0x0001);  // AE was read just above: cleared
  CHECK_EQ(b.read16(kDmaor), 0x0001u);
  s.m.run(100);
  CHECK_EQ(s.dmac.transfers(), 1u);
  // Reserved space (between the two decoded windows) is an address error too.
  b.write32(kSar1, 0x40000000u);
  b.write32(kDar1, System::kDst);
  b.write32(kTcr1, 1);
  b.write32(kChcr1, kRsAuto | kDe);
  CHECK_EQ(s.wait_vector(50), 10u);
  CHECK_EQ(b.read16(kDmaor) & 0x0004, 0x0004u);
}

void test_disabled() {
  System s;
  Bus& b = s.bus();
  b.write32(kSar0, System::kSrc);
  b.write32(kDar0, System::kDst);
  b.write32(kTcr0, 4);
  // DE = 0, DME = 1.
  b.write16(kDmaor, 0x0001);
  b.write32(kChcr0, kDmInc | kSmInc | kRsAuto | kTm | kTsLong);
  s.m.run(100);
  CHECK_EQ(s.dmac.transfers(), 0u);
  CHECK_EQ(b.read32(System::kDst), 0u);
  // DE = 1, DME = 0.
  b.write16(kDmaor, 0x0000);
  b.write32(kChcr0, kDmInc | kSmInc | kRsAuto | kTm | kTsLong | kDe);
  s.m.run(100);
  CHECK_EQ(s.dmac.transfers(), 0u);
  CHECK_EQ(b.read32(kTcr0), 4u);
  // Both: goes.
  b.write16(kDmaor, 0x0001);
  s.m.run(100);
  CHECK_EQ(s.dmac.transfers(), 4u);
  CHECK(s.block_copied(4));
}

void test_external_request() {
  System s;
  Bus& b = s.bus();
  int dack = 0, drak = 0;
  bool dack_level = false, drak_level = false;
  s.dmac.set_dack_sink(0, [&](bool high) { ++dack; dack_level = high; });
  s.dmac.set_drak_sink(0, [&](bool high) { ++drak; drak_level = high; });
  b.write32(kSar0, System::kSrc);
  b.write32(kDar0, System::kDst);
  b.write32(kTcr0, 4);
  b.write16(kDmaor, 0x0001);
  // Edge detection, cycle steal, dual address, active-low DACK (AL = 1).
  b.write32(kChcr0, kDmInc | kSmInc | kRsExtDual | kDs | kTsLong | kDe | (1u << 16));
  s.m.run(20);
  CHECK_EQ(s.dmac.transfers(), 0u);
  s.dmac.set_dreq(0, true);
  s.m.run(Dmac::kRequestLatency + 2);
  CHECK_EQ(s.dmac.transfers(), 1u);  // one unit per falling edge
  CHECK_EQ(drak, 2);                 // one DRAK pulse (active high: RL = 0)
  CHECK_EQ(drak_level, false);
  CHECK_EQ(dack, 2);                 // one DACK pulse, back to the inactive (high) level
  CHECK_EQ(dack_level, true);
  s.m.run(20);
  CHECK_EQ(s.dmac.transfers(), 1u);  // still low: no new edge
  s.dmac.set_dreq(0, false);
  s.dmac.set_dreq(0, true);
  s.m.run(Dmac::kRequestLatency + 2);
  CHECK_EQ(s.dmac.transfers(), 2u);
  // Level detection: transfers continue while DREQ is low.
  s.dmac.set_dreq(0, false);
  b.write32(kChcr0, b.read32(kChcr0) & ~kDs);
  s.dmac.set_dreq(0, true);
  s.m.run(20);
  CHECK_EQ(s.dmac.transfers(), 4u);
  CHECK((b.read32(kChcr0) & kTe) != 0);
  CHECK(s.block_copied(4));
  // Burst + edge on channel 1: the whole block on one edge.
  b.write32(kSar1, System::kSrc);
  b.write32(kDar1, System::kDst + 0x100);
  b.write32(kTcr1, 16);
  b.write32(kChcr1, kDmInc | kSmInc | kRsExtDual | kDs | kTm | kTsLong | kDe);
  s.dmac.set_dreq(1, true);
  s.m.run(Dmac::kRequestLatency + 2);
  CHECK_EQ(s.dmac.transfers(), 20u);
  CHECK((b.read32(kChcr1) & kTe) != 0);
}

void test_nmi() {
  System s;
  Bus& b = s.bus();
  b.write32(kSar0, System::kSrc);
  b.write32(kDar0, System::kDst);
  b.write32(kTcr0, 16);
  b.write16(kDmaor, 0x0001);
  b.write32(kChcr0, kDmInc | kSmInc | kRsAuto | kTsLong | kDe);  // cycle steal
  s.run_until_te(30);
  const u64 n = s.dmac.transfers();
  CHECK(n >= 2 && n < 16);
  s.dmac.on_nmi();
  CHECK_EQ(b.read16(kDmaor) & 0x0002, 0x0002u);  // NMIF
  s.m.run(200);
  CHECK_EQ(s.dmac.transfers(), n);  // suspended, TE not set
  CHECK_EQ(b.read32(kChcr0) & kTe, 0u);
  // Clearing NMIF (read above) resumes the remaining transfers.
  b.write16(kDmaor, 0x0001);
  CHECK_EQ(b.read16(kDmaor), 0x0001u);
  s.run_until_te(500);
  CHECK_EQ(s.dmac.transfers(), 16u);
  CHECK(s.block_copied());
  // NMIF is set even when the DMAC is idle (usage note 3).
  s.dmac.on_nmi();
  CHECK_EQ(b.read16(kDmaor), 0x0003u);
}

}  // namespace

int main() {
  test_reset_and_registers();
  test_burst_auto_request();
  test_cycle_steal_auto_request();
  test_module_request();
  test_address_error();
  test_disabled();
  test_external_request();
  test_nmi();
  return test::finish("test_sh2_dmac");
}

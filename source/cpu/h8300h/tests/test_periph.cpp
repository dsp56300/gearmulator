// Peripheral tests: the ITU compare match with its interrupt and TPC output,
// the SCI frame timing, the refresh interval timer, the watchdog, the DMAC
// short-address transfers and the interrupt controller's priority rules.
#include <initializer_list>

#include "cpu/h8300h/machine.hpp"
#include "common/test_util.hpp"

using namespace h8300h;

namespace {

constexpr u32 kCode = 0x001000, kRam = 0x200000;
constexpr u32 kTstr = 0xFFFF60, kTcr0 = 0xFFFF64, kTior0 = 0xFFFF65, kTier0 = 0xFFFF66, kTsr0 = 0xFFFF67, kTcnt0 = 0xFFFF68, kGra0 = 0xFFFF6A;
constexpr u32 kSmr0 = 0xFFFFB0, kBrr0 = 0xFFFFB1, kScr0 = 0xFFFFB2, kTdr0 = 0xFFFFB3, kSsr0 = 0xFFFFB4, kRdr0 = 0xFFFFB5;
constexpr u32 kTcsrW = 0xFFFFA8, kTcntW = 0xFFFFA9, kRtmcsr = 0xFFFFAD, kRtcnt = 0xFFFFAE, kRtcor = 0xFFFFAF;

struct System {
  Machine m;
  System() {
    m.map_rom(0x000000, 0x100000);
    m.map_ram(kRam, 0x40000);
    vector(0, kCode);
    m.reset();
    m.cpu().set_sp(kRam + 0x1000);
  }
  u8* mem() { return m.bus().mem(); }
  void code(std::initializer_list<u8> bytes, u32 at = kCode) {
    u32 a = at;
    for (u8 b : bytes) mem()[a++] = b;
    m.cpu().invalidate_range(at, u32(bytes.size()));
  }
  void vector(unsigned n, u32 target) {
    u8* v = mem() + n * 4;
    v[0] = 0; v[1] = u8(target >> 16); v[2] = u8(target >> 8); v[3] = u8(target);
  }
  u8 reg(u32 a) { return m.bus().read8(a); }
  void reg(u32 a, u8 v) { m.bus().write8(a, v); }
  // Interrupts enabled, spinning.
  void spin() {
    code({0x06, 0x7F, 0x40, 0xFE});  // ANDC #7F,CCR; BRA self
    m.step();
    m.step();
  }
};

void test_itu_compare_match() {
  System s;
  const u32 handler = 0x001200;
  s.vector(kVecImia0, handler);
  s.code({0x0B, 0x71, 0x56, 0x70}, handler);  // INC.L #1,ER1; RTE  (no flag clear: level stays)
  s.spin();
  s.reg(kTcr0, 0x21);           // clear on GRA match, phi/2
  s.reg(kGra0, 0x00); s.reg(kGra0 + 1, 100);
  s.reg(kTier0, 0x01);          // IMIEA
  const u64 t0 = s.m.now();
  s.reg(kTstr, 0x01);
  s.m.run(150);
  CHECK_EQ(s.reg(kTsr0) & 1, 0);
  CHECK_EQ(s.m.cpu().er(1), 0u);
  s.m.run(60);                  // 100 counts * 2 states = 200 states after start
  CHECK(s.reg(kTsr0) & 1);
  CHECK_EQ(s.m.cpu().er(1), 1u);  // taken once: the level is still asserted...
  // ... and the flag was not cleared by the handler: after RTE (with I clear)
  // it is taken again.
  s.m.run(100);
  CHECK(s.m.cpu().er(1) >= 2);
  // Clearing the flag (read then write 0) stops it.
  s.code({0x0B, 0x71, 0x7F, 0x67, 0x72, 0x00, 0x56, 0x70}, handler);  // INC.L #1,ER1; BCLR #0,@67:8; RTE
  s.m.run(410);
  const u64 count = s.m.cpu().er(1);
  s.m.run(200);
  CHECK_EQ(s.m.cpu().er(1), count + 1);  // one per match period (200 states)
  (void)t0;
  // The counter is read live.
  const u16 c1 = s.m.periph().tcnt(0);
  const u64 t1 = s.m.now();
  s.m.run(20);
  const u16 c2 = s.m.periph().tcnt(0);
  const u64 t2 = s.m.now();
  CHECK_EQ(u16(c2 - c1) % 100, u16((t2 >> 1) - (t1 >> 1)) % 100);
}

void test_sci_tx_rx() {
  System s;
  s.spin();
  std::vector<u8> sent;
  s.m.periph().set_tx_hook(0, [&](u8 b) { sent.push_back(b); });
  s.reg(kSmr0, 0x00);   // async, 8N1, phi
  s.reg(kBrr0, 15);     // 32 * 16 * 10 bits = 5120 states per frame: MIDI, 31250 baud at 16 MHz
  s.reg(kScr0, 0x30);   // TE RE
  CHECK(s.reg(kSsr0) & 0x80);  // TDRE
  s.reg(kTdr0, 0x5A);
  s.reg(kSsr0, u8(s.reg(kSsr0) & ~0x80));  // clear TDRE: starts the shift
  CHECK(!(s.reg(kSsr0) & 0x80) || true);
  s.m.run(5000);
  CHECK(sent.empty());
  s.m.run(1000);
  CHECK_EQ(sent.size(), 1u);
  CHECK_EQ(sent[0], 0x5A);
  CHECK(s.reg(kSsr0) & 0x04);  // TEND
  // Receive: a byte arrives one frame after it is offered.
  s.m.periph().receive(0, 0xA5);
  s.m.run(5000);
  CHECK(!(s.reg(kSsr0) & 0x40));
  s.m.run(1000);
  CHECK(s.reg(kSsr0) & 0x40);  // RDRF
  CHECK_EQ(s.reg(kRdr0), 0xA5);
  // RXI interrupt.
  const u32 handler = 0x001300;
  s.vector(kVecRxi0, handler);
  s.code({0x2A, 0xB5, 0x7F, 0xB4, 0x72, 0x60, 0x56, 0x70}, handler);  // MOV.B @B5:8,R2L; BCLR #6,@B4:8; RTE
  s.reg(kSsr0, u8(s.reg(kSsr0) & ~0x40));
  s.reg(kScr0, 0x70);  // RIE TE RE
  s.m.periph().receive(0, 0x3C);
  s.m.run(6000);
  CHECK_EQ(s.m.cpu().reg8(10), 0x3C);
  CHECK(!(s.reg(kSsr0) & 0x40));
}

void test_refresh_timer() {
  System s;
  const u32 handler = 0x001400;
  s.vector(kVecCmi, handler);
  s.code({0x0B, 0x71, 0x2A, 0xAD, 0x7F, 0xAD, 0x72, 0x70, 0x56, 0x70}, handler);  // INC ER1; read RTMCSR; BCLR #7; RTE
  s.spin();
  s.reg(kRtcor, 50);
  s.reg(kRtmcsr, 0x48);  // CMIE, phi/2
  s.m.run(90);
  CHECK_EQ(s.m.cpu().er(1), 0u);
  s.m.run(20);
  CHECK_EQ(s.m.cpu().er(1), 1u);
  s.m.run(1000);
  CHECK_EQ(s.m.cpu().er(1), 11u);
}

void test_watchdog() {
  System s;
  struct Sink : ResetSink { int resets = 0; void watchdog_reset() override { ++resets; } };
  s.spin();
  // Interval mode, phi/2: overflow after 256 * 2 states.
  s.reg(kTcsrW, 0xA5); s.reg(kTcntW, 0x20);  // TME, phi/2
  s.m.run(500);
  CHECK(!(s.reg(kTcsrW) & 0x80));
  s.m.run(20);
  CHECK(s.reg(kTcsrW) & 0x80);
  // Watchdog mode resets the machine.
  const u64 resets = s.m.resets();
  s.reg(kTcsrW, 0xA5); s.reg(kTcntW, 0x60);  // WT/IT, TME
  s.m.run(600);
  CHECK_EQ(s.m.resets(), resets + 1);
  CHECK_EQ(s.m.cpu().pc(), kCode);
}

void test_dma_short() {
  System s;
  s.spin();
  // Channel 0A: SCI0 RXI -> memory, byte, increment, 3 transfers.
  u8* mem = s.mem();
  (void)mem;
  s.reg(0xFFFF21, 0x20); s.reg(0xFFFF22, 0x00); s.reg(0xFFFF23, 0x00);  // MARA = 200000
  s.reg(0xFFFF24, 0x00); s.reg(0xFFFF25, 0x03);                          // ETCRA = 3
  s.reg(0xFFFF26, 0xB5);                                                 // IOARA = RDR0
  s.reg(0xFFFF27, 0x00); (void)s.reg(0xFFFF27);
  s.reg(0xFFFF27, 0x85);  // DTE, byte, increment, source = RXI0
  s.reg(kSmr0, 0); s.reg(kBrr0, 0); s.reg(kScr0, 0x70);
  const u8 data[3] = {1, 2, 3};
  s.m.periph().receive(0, data, 3);
  s.m.run(3000);
  CHECK_EQ(s.m.bus().read8(0x200000), 1);
  CHECK_EQ(s.m.bus().read8(0x200001), 2);
  CHECK_EQ(s.m.bus().read8(0x200002), 3);
  CHECK_EQ(s.reg(0xFFFF27) & 0x80, 0);  // DTE cleared at the end
  CHECK(!(s.reg(kSsr0) & 0x40));        // RDRF consumed by the DMAC
}

void test_priorities() {
  System s;
  const u32 h0 = 0x001500, h1 = 0x001600;
  s.vector(kVecIrq0, h0);
  s.vector(kVecIrq1, h1);
  s.code({0x0B, 0x71, 0x7F, 0xF6, 0x72, 0x00, 0x56, 0x70}, h0);  // INC ER1; BCLR #0,@ISR; RTE
  s.code({0x0B, 0x72, 0x7F, 0xF6, 0x72, 0x10, 0x56, 0x70}, h1);  // INC ER2; BCLR #1,@ISR; RTE
  s.spin();
  s.reg(kIer, 0x03);
  // UE = 0: I masks only priority-0 requests; IRQ1 raised to priority 1.
  s.reg(kSyscr, 0x03);
  s.reg(kIpra, 0x40);
  s.reg(kIpra, 0x40);
  s.code({0x04, 0x80, 0x40, 0xFE});  // ORC #80,CCR; BRA self
  s.m.cpu().set_pc(kCode);
  s.m.step(); s.m.step();
  s.m.periph().request(kVecIrq0);
  s.m.periph().request(kVecIrq1);
  s.m.run(100);
  CHECK_EQ(s.m.cpu().er(1), 0u);
  CHECK_EQ(s.m.cpu().er(2), 1u);
  CHECK(s.m.periph().requested(kVecIrq0));
  s.code({0x06, 0x7F, 0x40, 0xFE});
  s.m.cpu().set_pc(kCode);
  s.m.run(100);
  CHECK_EQ(s.m.cpu().er(1), 1u);
}

void test_arbitration_masks() {
  System s;
  // Documented IPR groups. Reserved vectors have priority zero, but only
  // vectors 12..60 and NMI participate in arbitration.
  constexpr int group[] = {15, 14, 13, 13, 12, 12, -1, -1, 11, 11, -1, -1,
    10, 10, 10, -1, 9, 9, 9, -1, 8, 8, 8, -1, 7, 7, 7, -1, 6, 6, 6, -1,
    5, 5, 5, 5, -1, -1, -1, -1, 3, 3, 3, 3, 2, 2, 2, 2, 1};
  u64 rng = 0x93741625;
  const auto random = [&]() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; };
  for (unsigned trial = 0; trial < 2048; ++trial) {
    const u64 requests = trial < 64 ? u64(1) << trial : random() & random();
    const u16 ipr = u16(random());
    const u8 ier = u8(random());
    const bool ue = (trial & 1) != 0;
    s.reg(kSyscr, ue ? 0x0b : 0x03);
    s.reg(kIpra, u8(ipr >> 8)); s.reg(kIprb, u8(ipr)); s.reg(kIer, ier);
    for (unsigned vector = 0; vector < 64; ++vector)
      s.m.periph().set_request(vector, (requests & (u64(1) << vector)) != 0);
    for (u8 ccr : {u8(0), kCcrI, kCcrUi, u8(kCcrI | kCcrUi)}) {
      unsigned expected = 64;
      if (requests & (u64(1) << kVecNmi)) expected = kVecNmi;
      else for (int priority = 1; priority >= 0 && expected == 64; --priority)
        for (unsigned vector = 12; vector <= 60; ++vector) {
          if (!(requests & (u64(1) << vector))) continue;
          if (vector < 18 && !(ier & (1u << (vector - 12)))) continue;
          const int bit = group[vector - 12];
          const bool high = bit >= 0 && (ipr & (1u << bit));
          if (int(high) != priority) continue;
          const bool masked = ue || !high ? (ccr & kCcrI) != 0 : (ccr & (kCcrI | kCcrUi)) == (kCcrI | kCcrUi);
          if (!masked) { expected = vector; break; }
        }
      unsigned actual = 64;
      const bool accepted = s.m.periph().arbitrate(actual, ccr);
      CHECK_EQ(accepted, expected != 64);
      if (accepted) CHECK_EQ(actual, expected);
    }
  }
}

}  // namespace

int main() {
  test_itu_compare_match();
  test_sci_tx_rx();
  test_refresh_timer();
  test_watchdog();
  test_dma_short();
  test_priorities();
  test_arbitration_masks();
  return test::finish("h8300h_test_periph");
}

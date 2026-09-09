// SH7014 multifunction timer pulse unit (manual section 10).
#include <vector>

#include "cpu/sh2/machine.hpp"
#include "cpu/sh2/mtu.hpp"
#include "common/test_util.hpp"

using namespace sh2;

namespace {

constexpr u32 kIprd = 0xFFFF834Eu, kIpre = 0xFFFF8350u;
constexpr u32 kC0 = Mtu::kBase0, kC1 = Mtu::kBase1, kC2 = Mtu::kBase2;

struct System {
  Machine m;
  Mtu mtu;
  static constexpr u32 kRam = 0xFFFFF000u;
  System() : m(ChipModel::SH7014, 1), mtu(m.sched(), m.cpu(), m.intc()) {
    m.bus().map_ram(0x00000000, 0x10000, Bus::kClsCs0);
    // Vectors: every interrupt handler writes its vector number to H'F100 and returns.
    for (u32 v = 64; v < 160; ++v) {
      const u32 h = 0x1000 + (v - 64) * 16;
      m.bus().write32(v * 4, h);
      const u16 code[] = {u16(0xE000 | (v & 0x7F)), 0x2102, 0x002B, 0x0009};  // MOV #v,R0 ; MOV.L R0,@R1 ; RTE ; NOP
      u32 a = h;
      for (u16 w : code) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    }
    const u16 loop[] = {0x0009, 0xAFFD, 0x0009};  // NOP ; BRA -6 ; slot NOP
    u32 a = kRam;
    for (u16 w : loop) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    m.bus().write32(0, kRam);
    m.bus().write32(4, 0xFFFFFBF0u);
    m.cpu().invalidate_all();
    m.reset();
    m.bus().write16(0xFFFF8624, 0x0000);  // WCR1: zero wait states (reset gives 15 to every area)
    mtu.map(m.io());
    mtu.reset();
    m.cpu().regs().sr &= ~Cpu::kIMask;
    m.cpu().regs().r[1] = 0xF100;
    m.bus().write32(0xF100, 0);
    // MTU0 / MTU1 level 7 (IPRD), MTU2 level 7 (IPRE).
    m.bus().write16(kIprd, 0x7777);
    m.bus().write16(kIpre, 0x7700);
  }
  Bus& bus() { return m.bus(); }
  u8 rd8(u32 a) { return bus().read8(a); }
  u16 rd16(u32 a) { return bus().read16(a); }
  void wr8(u32 a, u8 v) { bus().write8(a, v); }
  void wr16(u32 a, u16 v) { bus().write16(a, v); }
  void mask(bool on) {
    if (on) m.cpu().regs().sr |= Cpu::kIMask; else m.cpu().regs().sr &= ~Cpu::kIMask;
  }
  u64 run_until(u64 t) {
    while (m.now() < t) m.run(1);
    return m.now();
  }
  u32 marker() { return bus().read32(0xF100); }
  u32 wait_vector(u64 max, u64* at = nullptr) {
    bus().write32(0xF100, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    if (at) *at = m.now();
    return marker();
  }
  bool no_event() { return m.sched().next_time() == emu::Scheduler::kNever; }
};

void test_reset_and_reserved_bits() {
  System s;
  CHECK_EQ(s.rd8(Mtu::kTstr), 0x00);
  CHECK_EQ(s.rd8(Mtu::kTsyr), 0x00);
  for (u32 base : {kC0, kC1, kC2}) {
    CHECK_EQ(s.rd8(base + Mtu::kTcr), 0x00);
    CHECK_EQ(s.rd8(base + Mtu::kTmdr), 0xC0);
    CHECK_EQ(s.rd8(base + Mtu::kTiorH), 0x00);
    CHECK_EQ(s.rd8(base + Mtu::kTier), 0x40);
    CHECK_EQ(s.rd8(base + Mtu::kTsr), 0xC0);
    CHECK_EQ(s.rd16(base + Mtu::kTcnt), 0x0000);
    CHECK_EQ(s.rd16(base + Mtu::kTgrA), 0xFFFF);
    CHECK_EQ(s.rd16(base + Mtu::kTgrB), 0xFFFF);
  }
  CHECK_EQ(s.rd8(kC0 + Mtu::kTiorL), 0x00);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTgrC), 0xFFFF);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTgrD), 0xFFFF);
  CHECK_EQ(s.rd8(kC1 + Mtu::kTiorL), 0xFF);  // no TIOR1L: unassigned byte
  // Reserved bits.
  s.wr8(Mtu::kTstr, 0xFF);
  CHECK_EQ(s.rd8(Mtu::kTstr), 0x07);
  s.wr8(Mtu::kTstr, 0x00);
  s.wr8(Mtu::kTsyr, 0xFF);
  CHECK_EQ(s.rd8(Mtu::kTsyr), 0x07);
  s.wr8(Mtu::kTsyr, 0x00);
  s.wr8(kC1 + Mtu::kTcr, 0xFF);
  CHECK_EQ(s.rd8(kC1 + Mtu::kTcr), 0x7F);  // bit 7 reserved on channels 1, 2
  s.wr8(kC0 + Mtu::kTcr, 0xFF);
  CHECK_EQ(s.rd8(kC0 + Mtu::kTcr), 0xFF);
  s.wr8(kC0 + Mtu::kTmdr, 0x3F);
  CHECK_EQ(s.rd8(kC0 + Mtu::kTmdr), 0xFF);
  s.wr8(kC1 + Mtu::kTmdr, 0x3F);
  CHECK_EQ(s.rd8(kC1 + Mtu::kTmdr), 0xCF);  // no BFA / BFB
  s.wr8(kC0 + Mtu::kTier, 0xFF);
  CHECK_EQ(s.rd8(kC0 + Mtu::kTier), 0xDF);  // no TCIEU
  s.wr8(kC1 + Mtu::kTier, 0xFF);
  CHECK_EQ(s.rd8(kC1 + Mtu::kTier), 0xF3);  // no TGIEC / TGIED
  s.wr8(kC0 + Mtu::kTier, 0x40);
  s.wr8(kC1 + Mtu::kTier, 0x40);
  // Flags cannot be set by writing 1.
  s.wr8(kC0 + Mtu::kTsr, 0xFF);
  CHECK_EQ(s.rd8(kC0 + Mtu::kTsr), 0xC0);
  // 8-bit registers are accessible as a 16-bit pair (10.3.2), 16-bit ones as 32 bits.
  s.wr16(kC0 + Mtu::kTcr, 0x2100);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcr), 0x21C0);
  CHECK_EQ(s.bus().read32(kC0 + Mtu::kTcnt), 0x0000FFFFu);
  s.wr16(kC0 + Mtu::kTgrA, 0x1234);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTgrA), 0x1234);
  s.wr16(kC0 + Mtu::kTcnt, 0xABCD);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), 0xABCD);
  // Byte writes to 16-bit registers are prohibited and dropped.
  s.wr8(kC0 + Mtu::kTcnt, 0x55);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), 0xABCD);
  CHECK(s.no_event());
}

// TCNT0 counts every state (phi/1) from the TSTR write, on the phi/4 grid after
// a TCR change; a stopped counter holds; channels 1 and 2 have phi/256, phi/1024.
void test_counting_and_prescalers() {
  System s;
  const u64 t0 = s.m.now();
  s.wr8(Mtu::kTstr, 0x01);
  s.m.run(400);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), u16(s.m.now() - t0));
  CHECK(s.no_event());  // nothing enabled: lazy, no scheduler event
  // phi/4: counts on the multiples of 4 of the global state clock.
  s.wr8(kC0 + Mtu::kTcr, 0x01);
  const u64 t1 = s.m.now();
  const u16 c1 = s.rd16(kC0 + Mtu::kTcnt);
  s.m.run(400);
  CHECK_EQ(u16(s.rd16(kC0 + Mtu::kTcnt) - c1), u16(s.m.now() / 4 - t1 / 4));
  // Both edges of phi/4 = phi/2.
  s.wr8(kC0 + Mtu::kTcr, 0x11);
  const u64 t2 = s.m.now();
  const u16 c2 = s.rd16(kC0 + Mtu::kTcnt);
  s.m.run(400);
  CHECK_EQ(u16(s.rd16(kC0 + Mtu::kTcnt) - c2), u16(s.m.now() / 2 - t2 / 2));
  // Stop: the value holds.
  s.wr8(Mtu::kTstr, 0x00);
  const u16 held = s.rd16(kC0 + Mtu::kTcnt);
  s.m.run(300);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), held);
  // Channel 1 phi/256 and channel 2 phi/1024 (table 10.4).
  s.wr8(kC1 + Mtu::kTcr, 0x06);
  s.wr8(kC2 + Mtu::kTcr, 0x07);
  const u64 t3 = s.m.now();
  s.wr8(Mtu::kTstr, 0x06);
  s.m.run(8192);
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), u16(s.m.now() / 256 - t3 / 256));
  CHECK_EQ(s.rd16(kC2 + Mtu::kTcnt), u16(s.m.now() / 1024 - t3 / 1024));
}

// Compare match A with CCLR = TGRA: the match is signalled on the count clock
// following TCNT == TGRA, i.e. TGFA rises and TCNT restarts exactly at state
// t0 + TGRA + 1 (10.7.2: period = N + 1 states).
void test_compare_match_clear_and_interrupt() {
  System s;
  s.mask(true);
  s.wr16(kC0 + Mtu::kTgrA, 99);
  s.wr8(kC0 + Mtu::kTcr, 0x20);   // CCLR = TGRA compare match
  s.wr8(kC0 + Mtu::kTier, 0x41);  // TGIEA
  const u64 t0 = s.m.now();
  s.wr8(Mtu::kTstr, 0x01);
  CHECK(!s.no_event());  // the match can raise an interrupt: one event pending
  for (int i = 0; i < 260; ++i) {
    s.m.run(1);
    const u64 e = s.m.now() - t0;
    CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), u16(e % 100));
    CHECK_EQ((s.rd8(kC0 + Mtu::kTsr) & Mtu::kTgfa) != 0, e >= 100);
    CHECK_EQ(s.m.intc().request(IrqSrc::Tgi0a), e >= 100);
  }
  // Clear the flag (read 1 above, write 0) and take the next match as an interrupt.
  s.wr8(kC0 + Mtu::kTsr, 0x00);
  CHECK(!s.m.intc().request(IrqSrc::Tgi0a));
  const u64 e = s.m.now() - t0;
  const u64 next_match = t0 + (e / 100 + 1) * 100;
  s.mask(false);
  u64 at = 0;
  CHECK_EQ(s.wait_vector(300, &at), 88u);  // TGI0A
  CHECK(at >= next_match + Cpu::kIrqStates && at <= next_match + Cpu::kIrqStates + 16);
  CHECK(s.rd16(kC0 + Mtu::kTcnt) < 40);
}

void test_overflow() {
  System s;
  s.mask(true);
  s.wr16(kC0 + Mtu::kTcnt, 0xFFF0);
  s.wr8(kC0 + Mtu::kTier, 0x50);  // TCIEV
  const u64 t0 = s.m.now();
  s.wr8(Mtu::kTstr, 0x01);
  for (int i = 0; i < 30; ++i) {
    s.m.run(1);
    const u64 e = s.m.now() - t0;
    CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), u16(0xFFF0 + e));
    CHECK_EQ((s.rd8(kC0 + Mtu::kTsr) & Mtu::kTcfv) != 0, e >= 16);
  }
  s.mask(false);
  const u64 unmasked = s.m.now();
  u64 at = 0;
  CHECK_EQ(s.wait_vector(100, &at), 92u);  // TCI0V, pending since the overflow
  CHECK(at <= unmasked + Cpu::kIrqStates + 16);
  // Free-running counter keeps going from H'0000.
  CHECK(s.rd16(kC0 + Mtu::kTcnt) < 0x100);
  // Phase counting underflow on channel 1: TCFU and TCI1U (vector 101).
  s.wr8(kC1 + Mtu::kTmdr, 0xC4);  // phase counting mode 1
  s.wr8(kC1 + Mtu::kTier, 0x60);  // TCIEU
  s.wr8(Mtu::kTstr, 0x03);
  s.mtu.set_tclk(1, true);  // B rises with A low: count down from 0
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), 0xFFFF);
  CHECK((s.rd8(kC1 + Mtu::kTsr) & (Mtu::kTcfu | Mtu::kTcfd)) == Mtu::kTcfu);  // TCFD = 0: counting down
  s.wr8(kC0 + Mtu::kTsr, 0x00);  // drop TCI0V (read above)
  s.rd8(kC0 + Mtu::kTsr);
  s.wr8(kC0 + Mtu::kTsr, 0x00);
  CHECK_EQ(s.wait_vector(100), 101u);
}

// PWM mode 1 (figure 10.24): TGRA period (clear source, initial 0, output 0 on
// match), TGRB duty (output 1 on match); the waveform appears on TIOC0A only.
void test_pwm_mode_1() {
  System s;
  s.wr8(kC0 + Mtu::kTcr, 0x20);    // CCLR = TGRA, phi/1
  s.wr16(kC0 + Mtu::kTgrA, 9);     // period 10 states
  s.wr16(kC0 + Mtu::kTgrB, 3);     // rises on the count clock after TCNT == 3
  s.wr8(kC0 + Mtu::kTiorH, 0x21);  // IOB = output 1 on match, IOA = initial 0, output 0 on match
  s.wr8(kC0 + Mtu::kTmdr, 0xC2);   // PWM mode 1
  CHECK(!s.mtu.output(0, 0));
  const u64 t0 = s.m.now();
  s.wr8(Mtu::kTstr, 0x01);
  CHECK(s.no_event());  // outputs are resolved lazily unless a sink watches them
  for (int i = 0; i < 60; ++i) {
    s.m.run(1);
    const u64 e = s.m.now() - t0;
    CHECK_EQ(s.mtu.output(0, 0), (e % 10) >= 4);
    CHECK(!s.mtu.output(0, 1));
  }
  // 100% duty: TGRB == TGRA leaves the output unchanged (10.4.6).
  s.wr8(Mtu::kTstr, 0x00);
  s.wr16(kC0 + Mtu::kTcnt, 0);
  s.wr16(kC0 + Mtu::kTgrB, 9);
  s.wr8(kC0 + Mtu::kTiorH, 0x21);  // re-initialise the pin (counter stopped)
  CHECK(!s.mtu.output(0, 0));
  s.wr8(Mtu::kTstr, 0x01);
  s.m.run(50);
  CHECK(!s.mtu.output(0, 0));
  // An output sink sees every edge at its exact state.
  System w;
  w.wr8(kC0 + Mtu::kTcr, 0x20);
  w.wr16(kC0 + Mtu::kTgrA, 9);
  w.wr16(kC0 + Mtu::kTgrB, 3);
  w.wr8(kC0 + Mtu::kTiorH, 0x21);
  w.wr8(kC0 + Mtu::kTmdr, 0xC2);
  std::vector<std::pair<bool, u64>> edges;
  w.mtu.set_output_sink([&](unsigned ch, unsigned pin, bool level, u64 at) {
    if (ch == 0 && pin == 0) edges.emplace_back(level, at);
  });
  const u64 w0 = w.m.now();
  w.wr8(Mtu::kTstr, 0x01);
  // (A due event fires at the top of the next run slice: run a little past the last edge.)
  w.m.run(38);
  CHECK_EQ(edges.size(), size_t(7));
  if (edges.size() == 7) {
    const u64 expect[7] = {4, 10, 14, 20, 24, 30, 34};
    for (int i = 0; i < 7; ++i) {
      CHECK_EQ(edges[i].first, (i & 1) == 0);
      CHECK_EQ(edges[i].second, w0 + expect[i]);
    }
  }
}

// PWM mode 2: TGR1B period (cleared, no output), TGR1A duty with initial 0 and
// 1 on match; every pin returns to its initial level on the counter clear.
void test_pwm_mode_2() {
  System s;
  s.wr8(kC1 + Mtu::kTcr, 0x40);   // CCLR = TGRB
  s.wr16(kC1 + Mtu::kTgrB, 9);
  s.wr16(kC1 + Mtu::kTgrA, 5);
  s.wr8(kC1 + Mtu::kTiorH, 0x22);  // both: initial 0, output 1 on match
  s.wr8(kC1 + Mtu::kTmdr, 0xC3);   // PWM mode 2
  const u64 t0 = s.m.now();
  s.wr8(Mtu::kTstr, 0x02);
  for (int i = 0; i < 40; ++i) {
    s.m.run(1);
    const u64 e = s.m.now() - t0;
    CHECK_EQ(s.mtu.output(1, 0), (e % 10) >= 6);
    CHECK(!s.mtu.output(1, 1));  // the period register has no PWM output
  }
  // Duty == period: no change at all.
  s.wr16(kC1 + Mtu::kTgrA, 9);
  s.m.run(40);
  CHECK(!s.mtu.output(1, 0));
}

// Buffer operation (10.4.4): TGR0C is copied into TGR0A on compare match A and
// generates no match of its own; in input capture mode the old TGR0A moves to TGR0C.
void test_buffer_operation() {
  System s;
  s.wr8(kC0 + Mtu::kTcr, 0x40);    // CCLR = TGRB
  s.wr16(kC0 + Mtu::kTgrB, 19);    // period 20
  s.wr16(kC0 + Mtu::kTgrA, 5);
  s.wr16(kC0 + Mtu::kTgrC, 10);
  s.wr8(kC0 + Mtu::kTiorH, 0x12);  // IOB: 0 on match, IOA: initial 0, 1 on match
  s.wr8(kC0 + Mtu::kTmdr, 0xD0);   // BFA
  const u64 t0 = s.m.now();
  s.wr8(Mtu::kTstr, 0x01);
  s.run_until(t0 + 7);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTgrA), 10u);  // transferred at the match (state t0 + 6)
  CHECK((s.rd8(kC0 + Mtu::kTsr) & Mtu::kTgfa) != 0);
  CHECK(s.mtu.output(0, 0));
  s.run_until(t0 + 13);
  CHECK((s.rd8(kC0 + Mtu::kTsr) & Mtu::kTgfc) == 0);  // buffer register: no compare match
  CHECK(!s.mtu.output(0, 2));
  s.wr16(kC0 + Mtu::kTgrC, 15);
  s.run_until(t0 + 32);  // second period: TCNT == 10 at t0 + 30, match at t0 + 31
  CHECK_EQ(s.rd16(kC0 + Mtu::kTgrA), 15u);
  // Input capture with buffer: TGRA <- TCNT, TGRC <- old TGRA.
  s.wr8(kC0 + Mtu::kTiorH, 0x18);  // IOA = capture on rising edge
  s.m.run(5);
  const u16 old_a = s.rd16(kC0 + Mtu::kTgrA);
  const u16 cnt = s.rd16(kC0 + Mtu::kTcnt);
  s.mtu.capture_edge(0, 0, true);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTgrA), cnt);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTgrC), old_a);
}

// Input capture on TIOC1A (rising edge), then both edges, with TGI1A (vector 96)
// and counter clear by the capture.
void test_input_capture() {
  System s;
  s.wr8(kC1 + Mtu::kTiorH, 0x08);  // IOA = input capture on rising edge
  const u64 t0 = s.m.now();
  s.wr8(Mtu::kTstr, 0x02);
  s.m.run(100);
  s.mtu.capture_edge(1, 0, false);  // no edge (level already low)
  CHECK_EQ(s.rd16(kC1 + Mtu::kTgrA), 0xFFFF);
  s.mtu.capture_edge(1, 0, true);
  CHECK_EQ(s.rd16(kC1 + Mtu::kTgrA), u16(s.m.now() - t0));
  CHECK((s.rd8(kC1 + Mtu::kTsr) & Mtu::kTgfa) != 0);
  s.m.run(50);
  s.mtu.capture_edge(1, 0, false);  // falling edge ignored
  CHECK(s.rd16(kC1 + Mtu::kTgrA) != u16(s.m.now() - t0));
  s.wr8(kC1 + Mtu::kTsr, 0x00);  // clear TGFA (read above)
  CHECK((s.rd8(kC1 + Mtu::kTsr) & Mtu::kTgfa) == 0);
  // Both edges, interrupt enabled, and CCLR = TGRA: the capture clears TCNT1.
  s.wr8(kC1 + Mtu::kTiorH, 0x0A);
  s.wr8(kC1 + Mtu::kTcr, 0x20);
  s.wr8(kC1 + Mtu::kTier, 0x41);
  s.m.run(20);
  const u16 before = s.rd16(kC1 + Mtu::kTcnt);
  s.mtu.capture_edge(1, 0, true);
  CHECK_EQ(s.rd16(kC1 + Mtu::kTgrA), before);
  CHECK(s.rd16(kC1 + Mtu::kTcnt) < 4);
  CHECK_EQ(s.wait_vector(100), 96u);
  // TGRB as output compare is unaffected by TIOC1B edges.
  s.mtu.capture_edge(1, 1, true);
  CHECK_EQ(s.rd16(kC1 + Mtu::kTgrB), 0xFFFF);
}

// Phase counting mode 1 (table 10.9): a two-phase encoder sequence counts up
// four times per cycle forwards and down backwards; TCFD follows the direction.
void test_phase_counting() {
  System s;
  s.wr8(kC1 + Mtu::kTcr, 0x01);   // TPSC ignored in phase counting mode
  s.wr8(kC1 + Mtu::kTmdr, 0xC4);  // phase counting mode 1
  s.wr8(Mtu::kTstr, 0x02);
  s.m.run(100);
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), 0u);  // no internal clock
  // Forward: A leads B.  (A, B): 00 -> 10 -> 11 -> 01 -> 00
  s.mtu.set_tclk(0, true);
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), 1u);
  s.mtu.set_tclk(1, true);
  s.mtu.set_tclk(0, false);
  s.mtu.set_tclk(1, false);
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), 4u);
  CHECK((s.rd8(kC1 + Mtu::kTsr) & Mtu::kTcfd) != 0);
  // Backward: B leads A.  00 -> 01 -> 11 -> 10 -> 00
  s.mtu.set_tclk(1, true);
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), 3u);
  CHECK((s.rd8(kC1 + Mtu::kTsr) & Mtu::kTcfd) == 0);
  s.mtu.set_tclk(0, true);
  s.mtu.set_tclk(1, false);
  s.mtu.set_tclk(0, false);
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), 0u);
  // Compare match / clear work in phase counting mode too.
  s.wr16(kC1 + Mtu::kTgrA, 2);
  s.wr8(kC1 + Mtu::kTcr, 0x20);
  s.mtu.set_tclk(0, true);
  s.mtu.set_tclk(1, true);
  s.mtu.set_tclk(0, false);  // third count: TCNT leaves 2 -> match A, cleared
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), 0u);
  CHECK((s.rd8(kC1 + Mtu::kTsr) & Mtu::kTgfa) != 0);
  s.mtu.set_tclk(1, false);
  // Mode 2 (table 10.10): only falling edges of the A phase count, B gives the direction.
  s.wr8(kC1 + Mtu::kTmdr, 0xC5);
  s.wr16(kC1 + Mtu::kTcnt, 100);
  s.mtu.set_tclk(0, true);   // A rising: don't care
  s.mtu.set_tclk(1, true);   // B rising: don't care
  s.mtu.set_tclk(0, false);  // A falling with B high: up
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), 101u);
  s.mtu.set_tclk(1, false);
  s.mtu.set_tclk(0, true);
  s.mtu.set_tclk(0, false);  // A falling with B low: down
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), 100u);
  // Channel 2 uses TCLKC / TCLKD (table 10.8), mode 4: only B edges count.
  s.wr8(kC2 + Mtu::kTmdr, 0xC7);
  s.wr8(Mtu::kTstr, 0x06);
  s.mtu.set_tclk(2, true);   // A edge: don't care
  CHECK_EQ(s.rd16(kC2 + Mtu::kTcnt), 0u);
  s.mtu.set_tclk(3, true);   // B rising with A high: up
  CHECK_EQ(s.rd16(kC2 + Mtu::kTcnt), 1u);
  s.mtu.set_tclk(2, false);
  s.mtu.set_tclk(3, false);  // B falling with A low: up
  CHECK_EQ(s.rd16(kC2 + Mtu::kTcnt), 2u);
}

// External clock TCLKA on channel 0 with the CKEG edge selections; a compare
// match on the external count clears the counter and raises TGI0A.
void test_external_clock() {
  System s;
  s.wr8(kC0 + Mtu::kTcr, 0x04);  // TCLKA, rising edges
  s.wr8(Mtu::kTstr, 0x01);
  s.m.run(100);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), 0u);
  s.mtu.external_clock_edge(0, true);
  s.mtu.external_clock_edge(0, false);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), 1u);
  s.wr8(kC0 + Mtu::kTcr, 0x0C);  // falling edges
  s.mtu.external_clock_edge(0, true);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), 1u);
  s.mtu.external_clock_edge(0, false);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), 2u);
  s.wr8(kC0 + Mtu::kTcr, 0x14);  // both edges
  s.mtu.external_clock_edge(0, true);
  s.mtu.external_clock_edge(0, false);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), 4u);
  // TCLKB edges do not clock a channel on TCLKA.
  s.mtu.external_clock_edge(1, true);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), 4u);
  // Match on TGR0A = 5 with clear and interrupt.
  s.wr16(kC0 + Mtu::kTgrA, 5);
  s.wr8(kC0 + Mtu::kTcr, 0x24);  // CCLR = TGRA, TCLKA rising
  s.wr8(kC0 + Mtu::kTier, 0x41);
  s.mtu.external_clock_edge(0, true);  // 5
  s.mtu.external_clock_edge(0, false);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), 5u);
  s.mtu.external_clock_edge(0, true);  // leaves 5: match, clear
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), 0u);
  CHECK_EQ(s.wait_vector(100), 88u);
}

// Synchronous operation (10.4.3): channel 1 (CCLR = synchronous clear) is
// cleared together with channel 0 by TGR0A compare match; a TCNT write to a
// synchronised channel presets all of them.  Channel 2 stays independent.
void test_synchronous_clear_and_preset() {
  System s;
  s.wr8(Mtu::kTsyr, 0x03);
  s.wr8(kC0 + Mtu::kTcr, 0x20);  // CCLR = TGRA
  s.wr16(kC0 + Mtu::kTgrA, 49);  // period 50
  s.wr8(kC1 + Mtu::kTcr, 0x60);  // CCLR = synchronous clear, phi/1
  s.wr8(kC1 + Mtu::kTier, 0x42); // TGIEB on TGR1B = 60: never reached
  s.wr16(kC1 + Mtu::kTgrB, 60);
  const u64 t0 = s.m.now();
  s.wr8(Mtu::kTstr, 0x07);
  for (int i = 0; i < 130; ++i) {
    s.m.run(1);
    const u64 e = s.m.now() - t0;
    CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), u16(e % 50));
    CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), u16(e % 50));
    CHECK_EQ(s.rd16(kC2 + Mtu::kTcnt), u16(e));
  }
  CHECK((s.rd8(kC1 + Mtu::kTsr) & Mtu::kTgfb) == 0);
  CHECK(!s.m.intc().request(IrqSrc::Tgi1b));
  // A reachable TGR1B raises TGI1B at the exact state: TCNT1 == 30 at the
  // period start + 30, match one state later.
  s.wr16(kC1 + Mtu::kTgrB, 30);
  const u64 e = s.m.now() - t0;
  const u64 expect = t0 + (e / 50) * 50 + 31 + (e % 50 >= 31 ? 50 : 0);
  s.mask(true);
  while (s.m.now() < expect + 3) {
    s.m.run(1);
    const bool due = s.m.now() >= expect;
    CHECK_EQ((s.mtu.tsr(1) & Mtu::kTgfb) != 0, due);  // (the access resolves the lazy state)
    CHECK_EQ(s.m.intc().request(IrqSrc::Tgi1b), due);
  }
  s.mask(false);
  CHECK_EQ(s.wait_vector(50), 97u);
  // Synchronous preset.
  s.wr16(kC1 + Mtu::kTcnt, 0x1000);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), 0x1000);
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), 0x1000);
  CHECK(s.rd16(kC2 + Mtu::kTcnt) != 0x1000);
  s.wr16(kC2 + Mtu::kTcnt, 0x2000);  // not synchronised: only itself
  CHECK_EQ(s.rd16(kC2 + Mtu::kTcnt), 0x2000);
  CHECK_EQ(s.rd16(kC0 + Mtu::kTcnt), 0x1000);
  // A synchronised channel on another grid (phi/4) is cleared at the same state.
  s.wr8(kC1 + Mtu::kTier, 0x40);
  s.wr8(kC1 + Mtu::kTcr, 0x61);
  s.wr16(kC0 + Mtu::kTcnt, 0);
  const u64 t1 = s.m.now();
  s.run_until(t1 + 75);  // cleared at t1 + 50
  const u64 now = s.m.now();
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), u16(now / 4 - (t1 + 50) / 4));
}

// Flags clear only by writing 0 after reading 1; enabling an interrupt whose
// flag is already set requests at once; the DMAC clears TGFA directly.
void test_flag_clearing_protocol() {
  System s;
  s.mask(true);
  s.wr16(kC2 + Mtu::kTgrA, 10);
  s.wr8(Mtu::kTstr, 0x04);
  s.m.run(30);
  CHECK((s.mtu.tsr(2) & Mtu::kTgfa) != 0);  // introspection: flag set lazily
  s.wr8(kC2 + Mtu::kTsr, 0x00);  // no read yet: not cleared
  CHECK((s.rd8(kC2 + Mtu::kTsr) & Mtu::kTgfa) != 0);
  s.wr8(kC2 + Mtu::kTsr, 0xFF);  // writing 1 does not clear (nor set)
  CHECK((s.rd8(kC2 + Mtu::kTsr) & Mtu::kTgfa) != 0);
  s.wr8(kC2 + Mtu::kTsr, 0xFE);  // armed by the read: cleared
  CHECK((s.rd8(kC2 + Mtu::kTsr) & Mtu::kTgfa) == 0);
  // Enable after the flag is set: immediate request and vector 104.
  s.wr16(kC2 + Mtu::kTgrB, 40);
  s.m.run(30);
  CHECK((s.rd8(kC2 + Mtu::kTsr) & Mtu::kTgfb) != 0);
  CHECK(!s.m.intc().request(IrqSrc::Tgi2b));
  s.wr8(kC2 + Mtu::kTier, 0x42);
  CHECK(s.m.intc().request(IrqSrc::Tgi2b));
  s.mask(false);
  CHECK_EQ(s.wait_vector(50), 105u);
  s.wr8(kC2 + Mtu::kTsr, 0x00);  // read in the loop above? no: read now, then clear
  s.rd8(kC2 + Mtu::kTsr);
  s.wr8(kC2 + Mtu::kTsr, 0x00);
  CHECK(!s.m.intc().request(IrqSrc::Tgi2b));
  // DMAC activation clears TGFA without a read.
  s.wr8(kC2 + Mtu::kTier, 0x41);
  s.wr16(kC2 + Mtu::kTcnt, 0);
  s.wr16(kC2 + Mtu::kTgrA, 5);
  s.mask(true);
  s.m.run(10);
  CHECK(s.m.intc().request(IrqSrc::Tgi2a));
  s.mtu.dmac_clear(IrqSrc::Tgi2a);
  CHECK(!s.m.intc().request(IrqSrc::Tgi2a));
  CHECK((s.rd8(kC2 + Mtu::kTsr) & Mtu::kTgfa) == 0);
}

// Cascade connection (10.4.5): TCNT1 counts on TCNT2 overflow and underflow.
void test_cascade() {
  System s;
  s.wr8(kC1 + Mtu::kTcr, 0x07);  // count on TCNT2 overflow / underflow
  s.wr16(kC2 + Mtu::kTcnt, 0xFFFE);
  s.wr8(kC1 + Mtu::kTier, 0x41);  // TGIEA: TGR1A = 1 -> interrupt after the second overflow
  s.wr16(kC1 + Mtu::kTgrA, 1);
  s.mask(true);
  const u64 t0 = s.m.now();
  s.wr8(Mtu::kTstr, 0x06);
  s.run_until(t0 + 3);
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), 1u);
  CHECK((s.rd8(kC2 + Mtu::kTsr) & Mtu::kTcfv) != 0);
  CHECK(!s.m.intc().request(IrqSrc::Tgi1a));
  // Second overflow at t0 + 2 + 65536: TCNT1 leaves 1 -> TGF1A.
  s.wr16(kC2 + Mtu::kTcnt, 0xFFF0);
  const u64 t1 = s.m.now();
  while (s.m.now() < t1 + 20) {
    s.m.run(1);
    const bool due = s.m.now() >= t1 + 16;
    CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), due ? 2u : 1u);
    CHECK_EQ(s.m.intc().request(IrqSrc::Tgi1a), due);
  }
  // Underflow of channel 2 in phase counting mode counts channel 1 down.
  s.wr8(kC2 + Mtu::kTmdr, 0xC4);
  s.wr16(kC2 + Mtu::kTcnt, 0);
  s.mtu.set_tclk(3, true);  // TCLKD rises with TCLKC low: down
  CHECK_EQ(s.rd16(kC2 + Mtu::kTcnt), 0xFFFF);
  CHECK_EQ(s.rd16(kC1 + Mtu::kTcnt), 1u);
  CHECK((s.rd8(kC1 + Mtu::kTsr) & Mtu::kTcfd) == 0);
}

// TTGE: TGRA compare match requests an A/D conversion start at the exact state.
void test_adc_trigger_and_tior_initial_output() {
  System s;
  std::vector<std::pair<unsigned, u64>> trig;
  s.mtu.set_adc_trigger([&](unsigned ch, u64 at) { trig.emplace_back(ch, at); });
  s.wr8(kC0 + Mtu::kTcr, 0x20);
  s.wr16(kC0 + Mtu::kTgrA, 24);
  s.wr8(kC0 + Mtu::kTier, 0xC0);  // TTGE
  const u64 t0 = s.m.now();
  s.wr8(Mtu::kTstr, 0x01);
  s.m.run(60);
  CHECK_EQ(trig.size(), size_t(2));
  if (trig.size() == 2) {
    CHECK_EQ(trig[0].first, 0u);
    CHECK_EQ(trig[0].second, t0 + 25);
    CHECK_EQ(trig[1].second, t0 + 50);
  }
  // TIOR initial level while stopped: IOB = 0101 (initial 1, 0 on match) drives TIOC0B high;
  // output-disabled codes leave the pin alone.
  s.wr8(Mtu::kTstr, 0x00);
  CHECK(!s.mtu.output(0, 1));
  s.wr8(kC0 + Mtu::kTiorH, 0x50);
  CHECK(s.mtu.output(0, 1));
  s.wr8(kC0 + Mtu::kTiorH, 0x00);
  CHECK(s.mtu.output(0, 1));
  // Toggle output on match B every period.
  s.wr8(kC0 + Mtu::kTiorH, 0x30);  // initial 0, toggle on match
  s.wr16(kC0 + Mtu::kTgrB, 9);
  s.wr16(kC0 + Mtu::kTcnt, 0);
  CHECK(!s.mtu.output(0, 1));
  const u64 t1 = s.m.now();
  s.wr8(Mtu::kTstr, 0x01);
  s.run_until(t1 + 10);
  CHECK(s.mtu.output(0, 1));
  s.run_until(t1 + 35);
  CHECK(!s.mtu.output(0, 1));
}

}  // namespace

int main() {
  test_reset_and_reserved_bits();
  test_counting_and_prescalers();
  test_compare_match_clear_and_interrupt();
  test_overflow();
  test_pwm_mode_1();
  test_pwm_mode_2();
  test_buffer_operation();
  test_input_capture();
  test_phase_counting();
  test_external_clock();
  test_synchronous_clear_and_preset();
  test_flag_clearing_protocol();
  test_cascade();
  test_adc_trigger_and_tior_initial_output();
  return test::finish("test_sh2_mtu");
}

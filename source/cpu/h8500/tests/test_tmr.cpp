// 8-bit timer tests (H8/510 manual section 11).
#include "cpu/h8500/machine.hpp"
#include "common/test_util.hpp"

using namespace h8500;

namespace {

constexpr u32 kTcr = 0xFEC0, kTcsr = 0xFEC1, kTcora = 0xFEC2, kTcorb = 0xFEC3, kTcnt = 0xFEC4;

struct Board {
  Machine m;
  Board() : m(ChipModel::H8_510, 2) { m.bus().map_ram(0x0000, 0xFE80, BusClass::W16_S2); }
  Bus& bus() { return m.bus(); }
  Tmr& tmr() { return m.tmr(); }
  void poke(u32 addr, std::initializer_list<u8> bytes) {
    u32 a = addr;
    for (u8 b : bytes) m.bus().mem()[a++] = b;
  }
  void poke16(u32 addr, u16 v) { Bus::put_be16(m.bus().mem() + addr, v); }
  u16 peek16(u32 addr) { return Bus::be16(m.bus().mem() + addr); }
  void start() {
    poke16(0, 0x0100);
    poke(0x0100, {0x00, 0x00, 0x20, 0xFC});
    for (u32 v = 0x10; v < 0x80; v += 2) {
      const u16 h = u16(0x0500 + (v - 0x10) * 4);
      poke16(v, h);
      poke(h, {0x58, 0x00, u8(v), 0x1D, 0xF1, 0x00, 0x90, 0x0A});
    }
    m.cpu().invalidate_all();
    m.reset();
    m.cpu().regs().r[7] = 0xF000;
    m.cpu().regs().sr &= u16(~Cpu::kMaskBits);
    bus().write8(0xFF02, 0x50);  // 8-bit timer priority 5
  }
  u16 marker() { return peek16(0xF100); }
  u16 wait_vector(u64 max, u64* at = nullptr) {
    poke16(0xF100, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    if (at) *at = m.now();
    return marker();
  }
};

void test_registers() {
  Board b;
  b.start();
  Bus& bus = b.bus();
  CHECK_EQ(bus.read8(kTcr), 0x00);
  CHECK_EQ(bus.read8(kTcsr), 0x10);   // bit 4 reads 1
  CHECK_EQ(bus.read8(kTcora), 0xFF);
  CHECK_EQ(bus.read8(kTcorb), 0xFF);
  CHECK_EQ(bus.read8(kTcnt), 0x00);
  bus.write8(kTcsr, 0xEF);            // flags cannot be set, OS bits take
  CHECK_EQ(bus.read8(kTcsr), 0x1F);
  // Stopped: no counting.
  b.m.run(2000);
  CHECK_EQ(bus.read8(kTcnt), 0x00);
  CHECK(b.m.sched().empty());
}

void test_count_rates_and_overflow() {
  Board b;
  b.start();
  b.bus().write8(kTcr, 0x01);  // phi/8
  b.m.run(800);
  CHECK_EQ(b.bus().read8(kTcnt), u8(b.m.now() / 8));
  b.bus().write8(kTcr, 0x03);  // phi/1024
  const u8 before = b.bus().read8(kTcnt);
  b.m.run(1024 * 4);
  CHECK_EQ(u8(b.bus().read8(kTcnt) - before), 4);
  // Overflow interrupt: TCNT = H'FC at phi/64 -> OVI after 4 counts (256 states).
  b.bus().write8(kTcr, 0x02 | Tmr::kOvie);
  b.bus().write8(kTcnt, 0xFC);
  u64 at = 0;
  CHECK_EQ(b.wait_vector(600, &at), 0x64);  // OVI
  CHECK(at >= 256 - 64 + Cpu::kIrqStatesMin);  // grid-aligned: 4 ticks of 64 states
  CHECK((b.bus().read8(kTcsr) & Tmr::kOvf) != 0);
  CHECK(b.bus().read8(kTcnt) < 8);
}

// Compare match A clears the counter (CCLR = 01) and interrupts; the period
// is (TCORA + 1) counts.
void test_compare_match_a_periodic() {
  Board b;
  b.start();
  b.bus().write8(kTcora, 9);
  b.bus().write8(kTcr, 0x08 | 0x01 | Tmr::kCmiea);  // clear on A, phi/8, CMIA
  u64 t1 = 0, t2 = 0;
  CHECK_EQ(b.wait_vector(400, &t1), 0x60);  // CMIA
  CHECK((b.bus().read8(kTcsr) & Tmr::kCmfa) != 0);
  b.bus().write8(kTcsr, 0x00);              // clear (armed by the read)
  CHECK((b.bus().read8(kTcsr) & Tmr::kCmfa) == 0);
  CHECK_EQ(b.wait_vector(400, &t2), 0x60);
  // 10 counts of 8 states = 80 states apart (plus the same handler overhead).
  CHECK(t2 - t1 >= 80 - 8 && t2 - t1 <= 80 + 40);
  CHECK(b.bus().read8(kTcnt) <= 9);
}

// Compare match B with clear on B, and output select: 1 on A, 0 on B gives
// the pulse waveform of the manual's sample application (11.5).
void test_match_b_and_output() {
  Board b;
  b.start();
  b.bus().write8(kTcora, 3);
  b.bus().write8(kTcorb, 7);
  b.bus().write8(kTcsr, 0x06);            // OS: B -> 0, A -> 1
  b.bus().write8(kTcr, 0x10 | 0x01);      // clear on B, phi/8
  CHECK(!b.tmr().output());
  // Run to just after match A (tick 4 = state 32) but before B (state 64).
  b.m.run(40);
  CHECK(b.tmr().output());
  CHECK((b.bus().read8(kTcsr) & Tmr::kCmfa) != 0);
  CHECK((b.bus().read8(kTcsr) & Tmr::kCmfb) == 0);
  b.m.run(40);
  CHECK(!b.tmr().output());
  CHECK((b.bus().read8(kTcsr) & Tmr::kCmfb) != 0);
  CHECK(b.bus().read8(kTcnt) < 5);  // cleared at state 64, a few counts since (run() overshoot)
  // Simultaneous A and B match: toggle beats "0 output".
  b.bus().write8(kTcr, 0x00);
  b.bus().write8(kTcora, 0x20);
  b.bus().write8(kTcorb, 0x20);
  b.bus().write8(kTcsr, 0x07);            // B -> 0, A -> toggle
  b.bus().write8(kTcnt, 0x20);
  b.bus().write8(kTcr, 0x01);
  b.m.run(16);
  CHECK(b.tmr().output());
}

void test_external_clock_and_reset() {
  Board b;
  b.start();
  Tmr& t = b.tmr();
  b.bus().write8(kTcr, 0x18 | 0x05);  // clear on TMRI, external rising edges
  b.m.run(500);
  CHECK_EQ(b.bus().read8(kTcnt), 0);
  t.external_clock_edge(true);
  t.external_clock_edge(false);       // ignored
  t.external_clock_edge(true);
  CHECK_EQ(b.bus().read8(kTcnt), 2);
  b.bus().write8(kTcr, 0x18 | 0x07);  // both edges
  t.external_clock_edge(false);
  CHECK_EQ(b.bus().read8(kTcnt), 3);
  t.external_reset_edge();
  CHECK_EQ(b.bus().read8(kTcnt), 0);
}

}  // namespace

int main() {
  test_registers();
  test_count_rates_and_overflow();
  test_compare_match_a_periodic();
  test_match_b_and_output();
  test_external_clock_and_reset();
  return test::finish("test_tmr");
}

// 16-bit free-running timer tests (H8/510 manual section 10).
#include "cpu/h8500/machine.hpp"
#include "common/test_util.hpp"

using namespace h8500;

namespace {

constexpr u32 kTcr = 0xFEA0, kTcsr = 0xFEA1, kFrc = 0xFEA2, kOcra = 0xFEA4, kOcrb = 0xFEA6, kIcr = 0xFEA8;

struct Board {
  Machine m;
  Board() : m(ChipModel::H8_510, 2) { m.bus().map_ram(0x0000, 0xFE80, BusClass::W16_S2); }
  Cpu& cpu() { return m.cpu(); }
  Bus& bus() { return m.bus(); }
  Frt& frt() { return m.frt(0); }
  void poke(u32 addr, std::initializer_list<u8> bytes) {
    u32 a = addr;
    for (u8 b : bytes) m.bus().mem()[a++] = b;
  }
  void poke16(u32 addr, u16 v) { Bus::put_be16(m.bus().mem() + addr, v); }
  u16 peek16(u32 addr) { return Bus::be16(m.bus().mem() + addr); }
  // NOP loop at H'0100; every vector's handler records its address at H'F100.
  void start(u8 mask = 0) {
    poke16(0, 0x0100);
    poke(0x0100, {0x00, 0x00, 0x20, 0xFC});
    for (u32 v = 0x10; v < 0x80; v += 2) {
      const u16 h = u16(0x0500 + (v - 0x10) * 4);
      poke16(v, h);
      poke(h, {0x58, 0x00, u8(v), 0x1D, 0xF1, 0x00, 0x90, 0x0A});
    }
    m.cpu().invalidate_all();
    m.reset();
    cpu().regs().r[7] = 0xF000;
    cpu().regs().sr = u16((cpu().regs().sr & ~Cpu::kMaskBits) | (u16(mask) << Cpu::kMaskShift));
    bus().write8(0xFF01, 0x50);  // FRT1 priority 5
  }
  u16 marker() { return peek16(0xF100); }
  void clear_marker() { poke16(0xF100, 0); }
  // Run until a handler wrote the marker; returns the vector address (0 = none)
  // and leaves the time of the first handler instruction in `at`.
  u16 wait_vector(u64 max, u64* at = nullptr) {
    clear_marker();
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    if (at) *at = m.now();
    return marker();
  }
};

void test_registers_and_temp() {
  Board b;
  b.start();
  Bus& bus = b.bus();
  CHECK_EQ(bus.read8(kTcr), 0x00);
  CHECK_EQ(bus.read8(kTcsr), 0x00);
  CHECK_EQ(bus.read16(kOcra), 0xFFFF);
  CHECK_EQ(bus.read16(kOcrb), 0xFFFF);
  CHECK_EQ(bus.read16(kIcr), 0x0000);
  // Word write goes through TEMP (high byte latched, low byte commits).
  bus.write16(kOcra, 0x1234);
  CHECK_EQ(bus.read16(kOcra), 0x1234);
  // Writing only the high byte changes nothing until the low byte follows.
  bus.write8(kOcrb, 0xAB);
  CHECK_EQ(bus.read16(kOcrb), 0xFFFF);
  bus.write8(kOcrb + 1, 0xCD);
  CHECK_EQ(bus.read16(kOcrb), 0xABCD);
  // ICR is read-only.
  bus.write16(kIcr, 0x5555);
  CHECK_EQ(bus.read16(kIcr), 0x0000);
  // FRC read: high byte latches the low byte in TEMP.
  bus.write16(kFrc, 0x8001);
  CHECK_EQ(bus.read8(kFrc), 0x80);
  CHECK_EQ(bus.read8(kFrc + 1), 0x01);
  // Flag bits of TCSR cannot be written to 1.
  bus.write8(kTcsr, 0xFF);
  CHECK_EQ(bus.read8(kTcsr), 0x0F);
}

// FRC counts on the phi/4 grid from reset: after 400 states it reads 100,
// with no scheduler event involved (interrupts disabled).
void test_counting_and_prescalers() {
  Board b;
  b.start();
  b.m.run(400);  // stops at an instruction boundary at or after 400
  CHECK(b.m.now() >= 400 && b.m.now() < 410);
  CHECK_EQ(b.bus().read16(kFrc), u16(b.m.now() / 4));
  CHECK(b.m.sched().empty());
  // Switch to phi/32: 320 more states = 10 more counts.
  b.bus().write8(kTcr, 0x02);
  const u16 before = b.bus().read16(kFrc);
  b.m.run(320);
  CHECK_EQ(u16(b.bus().read16(kFrc) - before), 10u);
  // FRT2 counts independently at phi/8.
  b.bus().write8(0xFEB0, 0x01);
  b.bus().write16(0xFEB2, 0);
  const uint64_t started = b.m.now();
  b.m.run(800);
  CHECK_EQ(b.bus().read16(0xFEB2), u16(b.m.now() / 8 - started / 8));  // counts on the phi/8 grid
}

// Compare match A with counter clear and OCIA: the match signal fires on the
// increment following FRC == OCRA, i.e. at state (OCRA + 1) * 4.
void test_compare_match_clear_and_interrupt() {
  Board b;
  b.start();
  b.bus().write16(kOcra, 99);
  b.bus().write8(kTcsr, Frt::kCclra);
  b.bus().write8(kTcr, Frt::kOciea);
  u64 at = 0;
  CHECK_EQ(b.wait_vector(600, &at), 0x52);  // FRT1 OCIA
  // Match at state 400; entry (18) + MOV:I (3) + MOV.W store (7) = handler done by 428.
  CHECK(at >= 400 + Cpu::kIrqStatesMin && at <= 400 + Cpu::kIrqStatesMin + 12);
  CHECK((b.frt().tcsr(b.m.now()) & Frt::kOcfa) != 0);
  // The counter restarted from 0 at the match.
  const u16 frc = b.bus().read16(kFrc);
  CHECK(frc < 20);
  // The request stays until the flag is cleared (read 1, write 0).
  CHECK(b.m.intc().request(IrqSrc::Frt1Ocia));
  b.bus().write8(kTcsr, Frt::kCclra);  // write without a preceding read: no clear
  CHECK((b.bus().read8(kTcsr) & Frt::kOcfa) != 0);
  b.bus().write8(kTcsr, Frt::kCclra);  // now armed by the read above
  CHECK((b.bus().read8(kTcsr) & Frt::kOcfa) == 0);
  CHECK(!b.m.intc().request(IrqSrc::Frt1Ocia));
  // Periodic: the next match comes 100 counts later.
  u64 at2 = 0;
  CHECK_EQ(b.wait_vector(600, &at2), 0x52);
  CHECK(at2 > at && at2 - at <= 400 + 40);
}

void test_overflow_and_match_b() {
  Board b;
  b.start();
  b.bus().write16(kFrc, 0xFFF0);
  b.bus().write8(kTcr, Frt::kOvie);
  u64 at = 0;
  CHECK_EQ(b.wait_vector(200, &at), 0x56);  // FOVI after 16 counts = 64 states
  CHECK(at >= 64 && at <= 64 + 40);
  CHECK((b.bus().read8(kTcsr) & Frt::kOvf) != 0);
  CHECK(b.bus().read16(kFrc) < 0x40);
  b.bus().write8(kTcsr, 0);  // clear OVF (armed by the read above)
  // OCRB at the current count + 50, no clear: FRC keeps running through it.
  const u16 target = u16(b.bus().read16(kFrc) + 50);
  b.bus().write16(kOcrb, target);
  b.bus().write8(kTcr, Frt::kOcieb);
  CHECK_EQ(b.wait_vector(400, &at), 0x54);  // OCIB
  CHECK((b.bus().read8(kTcsr) & Frt::kOcfb) != 0);
  CHECK(b.bus().read16(kFrc) > target);
}

// A flag set while its interrupt was disabled requests as soon as it is enabled.
void test_late_enable_and_lazy_flags() {
  Board b;
  b.start();
  b.bus().write16(kOcra, 10);
  b.m.run(200);  // match at state 44 happened with interrupts off
  CHECK(b.m.sched().empty());
  CHECK((b.bus().read8(kTcsr) & Frt::kOcfa) != 0);
  CHECK(!b.m.intc().request(IrqSrc::Frt1Ocia));
  b.bus().write8(kTcr, Frt::kOciea);
  CHECK(b.m.intc().request(IrqSrc::Frt1Ocia));
  CHECK_EQ(b.wait_vector(100), 0x52);
}

void test_input_capture_and_external_clock() {
  Board b;
  b.start();
  Frt& f = b.frt();
  b.m.run(100);  // FRC = 25
  f.capture_edge(true);          // IEDG = 0: falling edges only
  CHECK((b.bus().read8(kTcsr) & Frt::kIcf) == 0);
  f.capture_edge(false);
  CHECK((b.bus().read8(kTcsr) & Frt::kIcf) != 0);
  CHECK_EQ(b.bus().read16(kIcr), 25u);
  b.bus().write8(kTcr, Frt::kIcie);
  CHECK_EQ(b.wait_vector(100), 0x50);  // ICI
  // External clock: the count follows pulses, not time.
  b.bus().write8(kTcr, 0x03);
  b.bus().write16(kFrc, 0x0100);
  b.m.run(1000);
  CHECK_EQ(b.bus().read16(kFrc), 0x0100u);
  for (int i = 0; i < 5; ++i) f.external_clock_pulse();
  CHECK_EQ(b.bus().read16(kFrc), 0x0105u);
  b.bus().write16(kOcra, 0x0106);
  b.bus().write8(kTcsr, Frt::kCclra);
  f.external_clock_pulse();  // -> 0x0106
  f.external_clock_pulse();  // match: clears
  CHECK_EQ(b.bus().read16(kFrc), 0u);
  CHECK((b.bus().read8(kTcsr) & Frt::kOcfa) != 0);
}

// Output-compare pins follow OLVL on match when enabled.
void test_output_compare_pins() {
  Board b;
  b.start();
  b.bus().write16(kOcra, 20);
  b.bus().write8(kTcsr, Frt::kOlvla);
  b.bus().write8(kTcr, Frt::kOea);
  CHECK(!b.frt().output_a());
  b.m.run(200);
  CHECK(b.frt().output_a());
}

}  // namespace

int main() {
  test_registers_and_temp();
  test_counting_and_prescalers();
  test_compare_match_clear_and_interrupt();
  test_overflow_and_match_b();
  test_late_enable_and_lazy_flags();
  test_input_capture_and_external_clock();
  test_output_compare_pins();
  return test::finish("test_frt");
}

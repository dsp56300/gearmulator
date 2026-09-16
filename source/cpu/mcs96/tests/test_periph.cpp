// Peripheral block tests: TIMER1 on the state clock, TIMER2 and its
// capture, ports, A/D timing and result format, PWM double buffering,
// interrupt vectors, HSI/HSO, serial transmission, power-down, the 8x9x
// port readback and the KB watchdog.
#include "cpu/mcs96/machine.hpp"
#include "common/test_util.hpp"

using namespace mcs96;
using Src = Peripherals::IrqSrc;

namespace {

struct System {
  Machine m;
  explicit System(Variant v = Variant::I80C196KB) : m(v) {
    m.bus().map_ram(0x0000, 0x10000);
    m.reset();
  }
  Cpu& cpu() { return m.cpu(); }
  Peripherals& p() { return m.periph(); }
  u8* mem() { return m.bus().mem(); }
  void nops(unsigned n) {
    for (unsigned i = 0; i < n; ++i) mem()[u16(cpu().regs().pc + i)] = 0xFD;
    cpu().invalidate_range(cpu().regs().pc, n);
  }
  // Run `states` of the machine (events included).
  void run(u64 states) { m.run(states); }
  u8 sfr(u8 a) { return cpu().reg8(a); }
  void set_sfr(u8 a, u8 v) { cpu().set_reg8(a, v); }
};

void test_timer1_state_clock() {
  System kb(Variant::I80C196KB), x9x(Variant::I8x9x);
  kb.nops(4);
  x9x.nops(2);
  kb.run(8);
  x9x.run(8);
  CHECK_EQ(kb.p().timer1(), 1);
  CHECK_EQ(x9x.p().timer1(), 1);
}

void test_kb_timer_registers_and_interrupts() {
  System s;
  s.set_sfr(0x14, 15);
  s.cpu().set_reg16(0x0A, 0xFFFF);
  CHECK_EQ(s.p().timer1(), 0xFFFF);
  s.set_sfr(0x14, 0);
  s.set_sfr(0x16, 0x0C);
  s.nops(8);
  s.run(8);
  CHECK_EQ(s.p().timer1(), 0);
  CHECK((s.p().interrupt_pending_low() & 0x01) != 0);
  CHECK((s.sfr(0x16) & 0x20) != 0);
  CHECK((s.sfr(0x16) & 0x20) == 0);

  s.cpu().set_reg16(0x0C, 0xFFFF);
  s.p().timer2_clock_transition();
  CHECK_EQ(s.p().timer2(), 0);
  CHECK((s.p().interrupt_pending_high() & 0x10) != 0);

  s.p().set_port_input(2, 0x7F);
  s.p().set_port_input(2, 0xFF);
  CHECK_EQ(s.p().timer2_capture(), 0);
  CHECK((s.p().interrupt_pending_high() & 0x08) != 0);

  s.set_sfr(0x0B, 0x02);
  s.p().set_port_input(2, 0xFF);
  s.p().timer2_clock_transition();
  CHECK_EQ(s.p().timer2(), 0xFFFF);
}

void test_port_output_hooks() {
  System s;
  u8 port1 = 0, port2 = 0;
  s.p().set_port_output_hook(1, [&](u8 v) { port1 = v; });
  s.p().set_port_output_hook(2, [&](u8 v) { port2 = v; });
  s.m.reset();
  CHECK_EQ(port1, 0xFF);
  CHECK_EQ(port2, 0xC1);
  s.set_sfr(0x0F, 0x5A);
  s.set_sfr(0x10, 0xFF);
  CHECK_EQ(port1, 0x5A);
  CHECK_EQ(port2, 0xE1);
}

void test_adc_timing_and_result_format() {
  System s;
  s.set_sfr(0x0B, 0x10);
  s.p().set_analog_input(3, 0x02AB);
  s.set_sfr(0x02, 0x0B);
  s.nops(64);
  s.run(8);
  CHECK((s.sfr(0x02) & 0x08) != 0);
  CHECK(s.p().adc_busy());
  s.run(80);  // 88 states elapsed: the 91-state conversion is still running
  CHECK(s.p().adc_busy());
  s.run(4);
  CHECK(!s.p().adc_busy());
  CHECK_EQ(s.sfr(0x03), 0xAA);
  CHECK_EQ(s.sfr(0x02), 0xC3);
  CHECK((s.p().interrupt_pending_low() & 0x02) != 0);
}

void test_8x9x_peripheral_profile() {
  System s(Variant::I8x9x);
  s.p().set_analog_input(1, 0x0155);
  s.set_sfr(0x02, 0x09);
  s.nops(32);
  s.run(84);
  CHECK(s.p().adc_busy());
  s.run(4);
  CHECK(!s.p().adc_busy());
  CHECK_EQ(s.sfr(0x03), 0x55);
  CHECK_EQ(s.sfr(0x02), 0x41);
  CHECK((s.p().interrupt_pending_low() & 0x02) != 0);

  s.m.reset();
  s.set_sfr(0x16, 0x08);
  for (unsigned i = 0; i < 0x10000; ++i) s.p().timer2_clock_transition();
  CHECK_EQ(s.p().timer2(), 0);
  CHECK((s.p().interrupt_pending_low() & 0x01) != 0);
  CHECK((s.sfr(0x16) & 0x10) != 0);

  s.p().set_port_input(0, 0x5A);
  s.p().set_port_input(1, 0xF0);
  s.set_sfr(0x0F, 0x3C);
  CHECK_EQ(s.sfr(0x0E), 0x5A);
  CHECK_EQ(s.sfr(0x0F), 0x30);
  CHECK_EQ(s.p().port_output(1), 0x3C);
}

void test_pwm_double_buffering() {
  System s;
  s.set_sfr(0x16, 0x01);
  s.set_sfr(0x17, 0x20);
  s.nops(160);
  s.run(254);
  CHECK_EQ(s.p().pwm_duty(), 0);
  CHECK(!s.p().pwm_output());
  s.run(2);
  CHECK_EQ(s.p().pwm_counter(), 0);
  CHECK_EQ(s.p().pwm_duty(), 0x20);
  CHECK(s.p().pwm_output());
  s.run(32);
  CHECK_EQ(s.p().pwm_counter(), 0x20);
  CHECK(!s.p().pwm_output());
}

void test_device_interrupt_vectors() {
  System kb;
  kb.cpu().set_sp(0x0080);
  kb.cpu().set_psw(kI);
  kb.set_sfr(0x13, 0x10);
  kb.p().request(Src::Timer2Overflow);
  kb.mem()[0x2038] = 0x34;
  kb.mem()[0x2039] = 0x12;
  CHECK_EQ(kb.cpu().step(), 16u);
  CHECK_EQ(kb.cpu().regs().pc, 0x1234);
  CHECK((kb.p().interrupt_pending_high() & 0x10) == 0);

  System x9x(Variant::I8x9x);
  x9x.cpu().set_sp(0x0080);
  x9x.p().set_nmi_input(true);
  CHECK_EQ(x9x.cpu().step(), 21u);
  CHECK_EQ(x9x.cpu().regs().pc, 0);
  CHECK_EQ(x9x.cpu().reg16(0x7E), kResetPc);
}

void test_hsi_and_hso_event_units() {
  System s;
  s.set_sfr(0x15, 0x01);
  s.set_sfr(0x03, 0x01);
  s.p().set_hsi_input(0, true);
  CHECK_EQ(s.p().hsi_fifo_count(), 1u);
  CHECK_EQ(s.sfr(0x06) & 0x03, 0x03);
  CHECK_EQ(s.cpu().reg16(0x04), 0);
  CHECK_EQ(s.p().hsi_fifo_count(), 0u);
  CHECK_EQ(s.p().interrupt_pending_low() & 0x14, 0x14);

  // HSO: set HSO.0 with an interrupt when TIMER1 = 1.
  s.set_sfr(0x06, 0x30);
  s.cpu().set_reg16(0x04, 1);
  s.nops(8);
  s.run(8);
  CHECK_EQ(s.p().timer1(), 1);
  CHECK((s.p().hso_output() & 0x01) != 0);
  CHECK((s.p().interrupt_pending_low() & 0x08) != 0);
  CHECK((s.sfr(0x17) & 0x01) != 0);
}

void test_serial_and_power_management() {
  System s;
  s.set_sfr(0x0E, 0x00);
  s.set_sfr(0x0E, 0x80);  // baud: divisor 1, asynchronous -> 16 states per bit
  s.set_sfr(0x07, 0xA5);
  s.nops(200);
  s.run(160);  // 10 bits
  CHECK((s.p().interrupt_pending_low() & 0x40) != 0);
  CHECK((s.p().interrupt_pending_high() & 0x01) != 0);
  CHECK((s.sfr(0x11) & 0x20) != 0);
  s.run(16);
  CHECK(!s.p().serial_tx_active());

  s.p().receive_serial(0x01AB);
  CHECK((s.p().interrupt_pending_high() & 0x02) != 0);
  CHECK_EQ(s.sfr(0x07), 0xAB);

  s.m.reset();
  s.set_sfr(0x16, 0x02);
  s.mem()[kResetPc] = 0xF6;
  s.mem()[kResetPc + 1] = 2;
  s.cpu().invalidate_all();
  CHECK_EQ(s.cpu().step(), 8u);
  CHECK(s.cpu().power_down());
  CHECK_EQ(s.cpu().step(), 1u);
  const u16 frozen = s.p().timer1();
  s.run(64);
  CHECK_EQ(s.p().timer1(), frozen);
  s.p().set_external_interrupt_input(0, true);
  CHECK(!s.cpu().power_down());
  CHECK((s.p().interrupt_pending_low() & 0x80) != 0);
}

void test_legacy_external_interrupt_port_readback() {
  System s(Variant::I8x9x);
  s.p().set_port_input(2, 0xFF);
  CHECK((s.sfr(0x10) & 0x04) == 0);
  s.p().set_external_interrupt_input(0, true);
  CHECK((s.sfr(0x10) & 0x04) != 0);
  s.p().set_external_interrupt_input(0, false);
  CHECK((s.sfr(0x10) & 0x04) == 0);
}

void test_watchdog_reset() {
  System s;
  s.set_sfr(0x0A, 0x1E);
  s.set_sfr(0x0A, 0xE1);
  CHECK(s.p().watchdog_enabled());
  s.cpu().set_reg16(0x20, 0x1234);
  s.nops(256);
  s.run(0x10000);
  CHECK_EQ(s.cpu().regs().pc, kResetPc);
  CHECK(!s.p().watchdog_enabled());
  CHECK_EQ(s.cpu().reg8(0x20), 0xFF);
  CHECK_EQ(s.m.resets(), 2u);
}

}  // namespace

int main() {
  test_timer1_state_clock();
  test_kb_timer_registers_and_interrupts();
  test_port_output_hooks();
  test_adc_timing_and_result_format();
  test_8x9x_peripheral_profile();
  test_pwm_double_buffering();
  test_device_interrupt_vectors();
  test_hsi_and_hso_event_units();
  test_serial_and_power_management();
  test_legacy_external_interrupt_port_readback();
  test_watchdog_reset();
  return test::finish("mcs96 periph");
}

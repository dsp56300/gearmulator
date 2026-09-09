// H8/532 peripheral set: chip configuration, the register-field layout, the
// interrupt controller (P1CR-gated IRQ0/IRQ1, three free-running timers), the
// PWM register block and port 9.
#include "cpu/h8500/machine.hpp"
#include "common/test_util.hpp"

using namespace h8500;

namespace {

// H8/532 in expanded maximum mode 3: external RAM below the on-chip RAM, a NOP
// loop at H'0100, and every interrupt vector (minimum-mode style: 4 bytes in
// maximum mode) pointing at a handler that stores its vector number at H'0F00
// and returns.
struct Board532 {
  Machine m;
  Board532() : m(ChipModel::H8_532, 3) { m.bus().map_ram(0x00000, 0xFB80, BusClass::W8_S3); }
  void poke(u32 addr, std::initializer_list<u8> bytes) {
    u32 a = addr;
    for (u8 b : bytes) m.bus().mem()[a++] = b;
  }
  void poke16(u32 addr, u16 v) { Bus::put_be16(m.bus().mem() + addr, v); }
  u16 peek16(u32 addr) { return Bus::be16(m.bus().mem() + addr); }
  void start() {
    poke16(0, 0x0000);
    poke16(2, 0x0100);
    poke(0x0100, {0x00, 0x00, 0x20, 0xFC});  // NOP NOP BRA -4
    for (u32 v = 32; v < 60; ++v) {
      const u16 h = u16(0x0500 + (v - 32) * 8);
      poke16(v * 4, 0x0000);
      poke16(v * 4 + 2, h);
      // MOV:I #v,R0 ; MOV.W R0,@H'0F00:16 ; RTE
      poke(h, {0x58, 0x00, u8(v), 0x1D, 0x0F, 0x00, 0x90, 0x0A});
    }
    m.cpu().invalidate_all();
    m.reset();
    m.cpu().regs().r[7] = 0x0F80;
    m.cpu().regs().sr &= u16(~Cpu::kMaskBits);
  }
  u16 marker() { return peek16(0x0F00); }
  u16 wait_vector(u64 max) {
    poke16(0x0F00, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    return marker();
  }
};

// The chip's own geometry: 20 address lines, 32 KiB masked ROM at 0, 1 KiB of
// RAM below a register field that runs H'FF80-H'FFFF.
void test_chip_config() {
  const ChipConfig c = make_chip_config(ChipModel::H8_532, 3);
  CHECK(c.max_mode());
  CHECK_EQ(c.address_bits(), 20u);
  CHECK_EQ(c.rom_size, 0x8000u);
  CHECK_EQ(c.ram_base, 0xFB80u);
  CHECK_EQ(c.ram_size, 0x400u);
  CHECK_EQ(c.regfield_base, 0xFF80u);
  CHECK_EQ(c.regfield_size, 0x80u);
  CHECK(!c.external_bus_16bit);
  // Modes 1 and 2 are expanded minimum, 7 is single chip.
  CHECK(!make_chip_config(ChipModel::H8_532, 1).max_mode());
  CHECK(make_chip_config(ChipModel::H8_532, 4).max_mode());
  CHECK(!make_chip_config(ChipModel::H8_532, 7).max_mode());
}

// Every peripheral answers at its H8/532 address rather than the H8/510's.
void test_register_map() {
  Machine m(ChipModel::H8_532, 3);
  const auto owns = [&m](u32 addr) { return m.io().owner(addr) != nullptr; };
  CHECK(owns(0xFF82));  // P1DR
  CHECK(owns(0xFF90));  // FRT1 TCR
  CHECK(owns(0xFFA0));  // FRT2 TCR
  CHECK(owns(0xFFB0));  // FRT3 TCR
  CHECK(owns(0xFFC0));  // PWM1 TCR
  CHECK(owns(0xFFD0));  // TMR TCR
  CHECK(owns(0xFFD8));  // SMR
  CHECK(owns(0xFFE0));  // ADDRA H
  CHECK(owns(0xFFEC));  // WDT TCSR
  CHECK(owns(0xFFF0));  // IPRA
  CHECK(owns(0xFFF9));  // RAMCR
  CHECK(owns(0xFFFE));  // P9DDR
  CHECK(owns(0xFFFF));  // P9DR
  // The H8/510's addresses are not decoded here.
  CHECK(!owns(0xFE82));
  CHECK(!owns(0xFEA0));
}

// MDCR reports the mode pins; RAMCR keeps only RAME; P1CR gates IRQ0/IRQ1.
void test_system_registers() {
  Machine m(ChipModel::H8_532, 3);
  m.reset();
  CHECK_EQ(m.bus().read8(0xFFFA) & 7, 3);         // MDCR = mode 3
  CHECK(m.sysregs532().ram_enabled());
  m.bus().write8(0xFFF9, 0x00);
  CHECK(!m.sysregs532().ram_enabled());
  CHECK_EQ(m.bus().read8(0xFFF9), 0x7F);          // reserved bits read as ones
  m.bus().write8(0xFFF9, 0x80);
  CHECK(m.sysregs532().ram_enabled());
  m.bus().write8(0xFFFC, 0x60);                   // IRQ0E + IRQ1E
  CHECK_EQ(m.intc().irqcr() & 3, 3);
  m.bus().write8(0xFFFC, 0x00);
  CHECK_EQ(m.intc().irqcr() & 3, 0);
}

// IRQ0 is level-sensed and IRQ1 edge-latched, and neither is taken until the
// program enables it in P1CR — the chip has no IRQCR.
void test_irq_gating() {
  Board532 b;
  b.start();
  b.m.bus().write8(0xFFF0, 0x77);   // IPRA: IRQ0 and IRQ1 at level 7

  b.m.intc().set_irq_pin(0, true);
  CHECK_EQ(b.wait_vector(200), 0);  // disabled in P1CR
  b.m.intc().set_irq_pin(0, false);

  b.m.bus().write8(0xFFFC, 0x20);   // IRQ0E
  b.m.intc().set_irq_pin(0, true);
  CHECK_EQ(b.wait_vector(200), 32);
  b.m.intc().set_irq_pin(0, false);

  b.m.bus().write8(0xFFFC, 0x40);   // IRQ1E only
  b.m.intc().set_irq_pin(1, true);
  CHECK_EQ(b.wait_vector(200), 33);
}

// The third free-running timer exists and posts its own vectors.
void test_frt3() {
  Board532 b;
  b.start();
  b.m.bus().write8(0xFFF2, 0x70);   // IPRC 6-4: FRT3 at level 7
  b.m.bus().write8(0xFFB0, Frt::kOciea);
  b.m.bus().write8(0xFFB5, 0x00);   // OCRA = H'0040
  b.m.bus().write8(0xFFB6, 0x40);
  CHECK_EQ(b.wait_vector(2000), 45);  // FRT3 OCIA
}

// PWM is a register model: the duty byte reads back and is what a board sees.
void test_pwm_registers() {
  Machine m(ChipModel::H8_532, 3);
  m.reset();
  m.bus().write8(0xFFC1, 0x40);
  m.bus().write8(0xFFC5, 0x80);
  m.bus().write8(0xFFC9, 0xC0);
  CHECK_EQ(m.bus().read8(0xFFC1), 0x40);
  CHECK_EQ(m.pwm532().duty(0), 0x40);
  CHECK_EQ(m.pwm532().duty(1), 0x80);
  CHECK_EQ(m.pwm532().duty(2), 0xC0);
}

// Port 9 lives outside the port block, and port 3 is the data bus in the
// expanded modes.
void test_ports() {
  Machine m(ChipModel::H8_532, 3);
  m.reset();
  m.ports().set_pins(9, 0x5A);
  CHECK_EQ(m.bus().read8(0xFFFF), 0x5A);   // all inputs: the pin levels
  m.bus().write8(0xFFFE, 0xFF);            // P9DDR: all outputs
  m.bus().write8(0xFFFF, 0xA5);
  CHECK_EQ(m.bus().read8(0xFFFF), 0xA5);   // the latch
  CHECK_EQ(m.bus().read8(0xFFFE), 0xFF);   // a DDR reads as all ones
  CHECK_EQ(m.bus().read8(0xFF86), 0xFF);   // P3DR: the data bus
  m.ports().set_pins(8, 0x3C);
  CHECK_EQ(m.bus().read8(0xFF8F), 0x3C);   // P8DR is input only
}

// Eight analog inputs: a single conversion of channel 7 reaches the sampler as
// channel 7 and lands in the fourth result register.
void test_adc_channels() {
  Board532 b;
  b.start();
  unsigned seen = 99;
  b.m.adc().set_sampler([&seen](const unsigned ch) -> u16 { seen = ch; return 0x2A0; });
  b.m.bus().write8(0xFFE8, Adc::kAdst | 7);   // ADCSR: single conversion, channel 7
  b.m.run(400);
  CHECK_EQ(seen, 7u);
  CHECK_EQ(b.m.adc().result(3), u16(0x2A0 << 6));
  CHECK(b.m.bus().read8(0xFFE8) & Adc::kAdf);
}

}  // namespace

int main() {
  test_adc_channels();
  test_chip_config();
  test_register_map();
  test_system_registers();
  test_irq_gating();
  test_frt3();
  test_pwm_registers();
  test_ports();
  return test::finish("h8532");
}

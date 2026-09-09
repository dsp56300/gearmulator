// SH7034 (SH-1) machine: memory map, SH-1 instruction subset, INTC layout,
// ITU, DMAC, BSC, WDT / A/D / SCI register placement, ports.
#include <initializer_list>

#include "cpu/sh2/machine7034.hpp"
#include "common/test_util.hpp"

using namespace sh2;

namespace {

struct System {
  Machine7034 m;
  static constexpr u32 kRom = 0x00001000u, kRam = 0x0FFFF000u, kRamMirror = 0x0F00F000u, kExtRam = 0x01000000u;
  static constexpr u32 kMarker = kRam + 0xF00;
  System() : m(2) {
    m.bus().map_ram(kExtRam, 0x10000, Bus::kClsDram);
    // Vectors 64-115: each handler writes its vector number to the marker and returns.
    for (u32 v = 4; v < 116; ++v) {
      const u32 h = 0x8000 + v * 16;
      poke32(v * 4, h);
      code(h, {u16(0xE000 | (v & 0x7F)), 0x2102, 0x002B, 0x0009});  // MOV #v,R0 ; MOV.L R0,@R1 ; RTE ; NOP
    }
    code(kRom, {0x0009, 0xAFFD, 0x0009});  // NOP ; BRA -6 ; NOP
    poke32(0, kRom);
    poke32(4, kRamMirror + 0xFF0);  // SP through a shadow of the RAM
    m.cpu().invalidate_all();
    m.reset();
    m.cpu().regs().sr &= ~Cpu::kIMask;
    m.cpu().regs().r[1] = kMarker;
  }
  // ROM contents are loaded, not written (writes to ROM lines are dropped).
  void poke32(u32 at, u32 v) {
    const u8 b[4] = {u8(v >> 24), u8(v >> 16), u8(v >> 8), u8(v)};
    m.bus().load(at, b, 4);
  }
  void code(u32 at, std::initializer_list<u16> words) {
    for (u16 w : words) { m.bus().load(at, reinterpret_cast<const u8*>(&(w = u16((w >> 8) | (w << 8)))), 2); at += 2; }
  }
  Bus& bus() { return m.bus(); }
  Cpu& cpu() { return m.cpu(); }
  u32 marker() { return bus().read32(kMarker); }
  u32 wait_vector(u64 max) {
    bus().write32(kMarker, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    return marker();
  }
};

void test_memory_map() {
  System s;
  // Data through a shadow of area 7 lands in the on-chip RAM; so does code.
  s.bus().write32(System::kRam + 0x100, 0x11223344);
  CHECK_EQ(s.bus().read32(System::kRamMirror + 0x100), 0x11223344u);
  s.code(System::kRom + 0x20, {0x6103, 0xE042, 0x2102, 0xAFFE, 0x0009});  // MOV R0,R1 ; MOV #H'42,R0 ; MOV.L R0,@R1 ; loop
  s.cpu().regs().pc = System::kRom + 0x20;
  s.cpu().regs().r[0] = System::kRamMirror + 0x200;
  s.m.run(20);
  CHECK_EQ(s.bus().read32(System::kRam + 0x200), 0x42u);
  // Code executes from the RAM shadow.
  s.code(System::kRam + 0x300, {0xE07E, 0xAFFE, 0x0009});  // MOV #H'7E,R0 ; loop
  s.cpu().regs().pc = System::kRamMirror + 0x300;
  s.m.run(10);
  CHECK_EQ(s.cpu().regs().r[0], 0x7Eu);
  // The on-chip ROM shows in both A27 halves of area 0.
  CHECK_EQ(s.bus().read16(0x08000000u + System::kRom), s.bus().read16(System::kRom));
  // The register field sits at H'5FFFE00: WDT TCSR, ADCSR, SCI0 SMR, IPRA read back their reset values.
  CHECK_EQ(s.bus().read8(0x05FFFFB8), 0x18);
  CHECK_EQ(s.bus().read8(0x05FFFEE8), 0x00);
  CHECK_EQ(s.bus().read8(0x05FFFEC0), 0x00);
  CHECK_EQ(s.bus().read16(0x05FFFF84), 0x0000);
  CHECK_EQ(s.bus().read16(0x05FFFFA2), 0xFFFF);  // WCR1
}

void test_sh1_subset() {
  System s;
  s.code(System::kRom + 0x40, {0x4010, 0x0009});  // DT R0: SH-2 only -> general illegal, vector 4
  s.cpu().regs().pc = System::kRom + 0x40;
  CHECK_EQ(s.wait_vector(50), 4u);
  // MULS.W exists on the SH-1.
  s.code(System::kRom + 0x60, {0x22FF, 0x001A, 0xAFFE, 0x0009});  // MULS.W R15,R2 ; STS MACL,R0
  s.cpu().regs().pc = System::kRom + 0x60;
  s.cpu().regs().r[2] = 3;
  s.cpu().regs().r[15] = 7;
  s.m.run(10);
  CHECK_EQ(s.cpu().regs().r[0], 21u);
}

void test_intc_layout() {
  System s;
  s.bus().write16(0x05FFFF86, 0x0500);  // IPRB: IRQ5 level 5
  s.m.set_irq_pin(5, true);
  CHECK_EQ(s.wait_vector(100), 69u);
  s.m.set_irq_pin(5, false);
  // Edge sensing through ICR bit 2 (IRQ5S): a second low level is not a new request.
  s.bus().write16(0x05FFFF8E, 0x0004);
  s.m.set_irq_pin(5, true);
  CHECK_EQ(s.wait_vector(100), 69u);
  CHECK_EQ(s.wait_vector(100), 0u);
  s.m.set_irq_pin(5, false);
  s.m.set_irq_pin(5, true);
  CHECK_EQ(s.wait_vector(100), 69u);
  s.m.set_irq_pin(5, false);
  CHECK_EQ(s.m.intc().vector_of(IrqSrc::Adi), 109u);
  CHECK_EQ(s.m.intc().vector_of(IrqSrc::Tgi3a), 92u);
}

void test_itu() {
  System s;
  Bus& b = s.bus();
  b.write16(0x05FFFF88, 0x0060);  // IPRC: ITU0 level 6
  b.write8(0x05FFFF04, 0x20);     // TCR0: clear on GRA, phi/1
  b.write16(0x05FFFF0A, 99);      // GRA0
  b.write8(0x05FFFF06, 0x01);     // TIER0: IMIEA
  b.write8(0x05FFFF00, 0x01);     // TSTR: start channel 0
  const u64 t0 = s.m.now();
  s.m.run(50);
  CHECK_EQ(s.m.itu().tcnt(0), u16(s.m.now() - t0));
  CHECK_EQ(s.wait_vector(200), 80u);
  CHECK(s.m.now() - t0 >= 99 && s.m.now() - t0 < 140);
  // The flag clears by reading 1 then writing 0; the counter wraps to 0 after GRA.
  CHECK(b.read8(0x05FFFF07) & 0x01);
  b.write8(0x05FFFF07, 0xF8);
  CHECK((b.read8(0x05FFFF07) & 0x01) == 0);
  CHECK(s.m.itu().tcnt(0) < 99);
  // Next period: 100 ticks later.
  const u64 t1 = s.m.now();
  CHECK_EQ(s.wait_vector(200), 80u);
  const u64 period_end = s.m.now();
  CHECK(period_end - t1 <= 100 + 12);  // 100 ticks minus the count already made, plus the interrupt latency
  // Overflow with GRA out of reach: OVF on channel 1 (phi/4) after 65536 counts.
  b.write16(0x05FFFF8A, 0x000A);  // IPRC bits 3-0? channel 1 is IPRC 3-0: use IPRC
  b.write16(0x05FFFF88, 0x0006);
  b.write8(0x05FFFF0E, 0x02);     // TCR1: phi/4
  b.write16(0x05FFFF12, 0xFFF0);  // TCNT1
  b.write8(0x05FFFF10, 0x04);     // TIER1: OVIE
  b.write8(0x05FFFF00, 0x02);     // TSTR: channel 1 only
  CHECK_EQ(s.wait_vector(200), 86u);
}

void test_dmac() {
  System s;
  Bus& b = s.bus();
  for (u32 i = 0; i < 4; ++i) b.write16(System::kExtRam + i * 2, u16(0x1230 + i));
  b.write16(0x05FFFF88, 0x7000);  // IPRC: DMAC0/1 level 7
  b.write32(0x05FFFF40, System::kExtRam);          // SAR0
  b.write32(0x05FFFF44, System::kExtRam + 0x100);  // DAR0
  b.write16(0x05FFFF4A, 4);                        // TCR0
  b.write16(0x05FFFF48, 0x0001);                   // DMAOR: DME
  b.write16(0x05FFFF4E, 0x5C1D);                   // CHCR0: DM/SM increment, auto-request, burst, word, IE, DE
  CHECK_EQ(s.wait_vector(200), 72u);
  for (u32 i = 0; i < 4; ++i) CHECK_EQ(b.read16(System::kExtRam + 0x100 + i * 2), u16(0x1230 + i));
  CHECK(b.read16(0x05FFFF4E) & 0x0002);  // TE
  CHECK_EQ(b.read16(0x05FFFF4A), 0u);
}

void test_bsc_timing() {
  System s;
  Bus& b = s.bus();
  // Reset: WCR1 = H'FFFF, WCR3 = H'F800 -> area 1 (DRAM) 2 states, area 2 1 + 4 states.
  CHECK_EQ(b.access_class(Bus::kClsDram).cycles(2), 2u);
  CHECK_EQ(b.access_class(Bus::kClsCs1).cycles(2), 5u);
  b.write16(0x05FFFFA2, 0x0000);  // WCR1: no waits
  b.write16(0x05FFFFA6, 0x8000);  // WCR3: long wait 1
  CHECK_EQ(b.access_class(Bus::kClsDram).cycles(2), 1u);
  CHECK_EQ(b.access_class(Bus::kClsCs1).cycles(2), 2u);
  CHECK_EQ(b.access_class(Bus::kClsArea6).cycles(2), 4u);
  // A word read from the DRAM area costs one state more with RW1 set.
  s.code(System::kRom + 0x80, {0x6501, 0xAFFE, 0x0009});  // MOV.W @R0,R5 ; loop
  s.cpu().regs().r[0] = System::kExtRam;
  s.cpu().regs().pc = System::kRom + 0x80;
  CHECK_EQ(s.cpu().step(), 1u);
  b.write16(0x05FFFFA2, 0x0200);  // RW1
  s.cpu().regs().pc = System::kRom + 0x80;
  CHECK_EQ(s.cpu().step(), 2u);
  // Refresh timer: RTCOR 9 at phi/2 -> CMF every 20 states, CMI (vector 113) with CMIE.
  b.write16(0x05FFFF8C, 0x0050);  // IPRE: WDT/REF level 5
  b.write16(0x05FFFFB2, 0x9609);  // RTCOR = 9
  b.write16(0x05FFFFAE, 0xA548);  // RTCSR: CMIE, CKS = 1 (phi/2)
  CHECK_EQ(s.wait_vector(100), 113u);
}

void test_ports_and_adc() {
  System s;
  Bus& b = s.bus();
  unsigned port = 9; u16 value = 0;
  s.m.ports().set_output_hook([&](unsigned p, u16 v) { port = p; value = v; });
  s.m.ports().set_input_b(0x00FF);
  b.write16(0x05FFFFC6, 0xFF00);  // PBIOR: upper byte outputs
  b.write16(0x05FFFFC2, 0xA5A5);  // PBDR
  CHECK_EQ(port, 1u);
  CHECK_EQ(value, 0xA500u);
  CHECK_EQ(b.read16(0x05FFFFC2), 0xA5FFu);  // outputs from the register, inputs from the pins
  s.m.ports().set_input_c(0x3C);
  CHECK_EQ(b.read16(0x05FFFFD0), 0x3Cu);
  // A/D at H'5FFFEE0: single conversion of AN2.
  s.m.adc().set_input(2, 0x2AB);
  b.write8(0x05FFFEE8, 0x22);  // ADST, CH2
  s.m.run(300);
  CHECK(b.read8(0x05FFFEE8) & 0x80);
  CHECK_EQ(b.read16(0x05FFFEE4), u16(0x2AB << 6));
}

}  // namespace

int main() {
  test_memory_map();
  test_sh1_subset();
  test_intc_layout();
  test_itu();
  test_dmac();
  test_bsc_timing();
  test_ports_and_adc();
  return test::finish("test_sh7034");
}

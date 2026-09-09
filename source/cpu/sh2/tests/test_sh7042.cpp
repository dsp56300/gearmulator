// SH7042: the SH7040-series superset of the SH7014 on the shared Machine.
#include <initializer_list>

#include "cpu/sh2/machine.hpp"
#include "common/test_util.hpp"

using namespace sh2;

namespace {

struct System {
  Machine m;
  static constexpr u32 kRam = 0xFFFFF000u, kExtRam = 0x00400000u, kMarker = kRam + 0xF00;
  System() : m(ChipModel::SH7042, 2) {
    m.bus().map_ram(kExtRam, 0x10000, Bus::kClsCs1);
    for (u32 v = 4; v < 160; ++v) {
      const u32 h = 0x8000 + v * 16;
      poke32(v * 4, h);
      code(h, {u16(0xE000 | (v & 0x7F)), 0x2102, 0x002B, 0x0009});  // MOV #(v & 7F),R0 ; MOV.L R0,@R1 ; RTE ; NOP
    }
    code(0x1000, {0x0009, 0xAFFD, 0x0009});
    poke32(0, 0x1000);
    poke32(4, kRam + 0xFF0);
    m.cpu().invalidate_all();
    m.reset();
    m.bus().write16(0xFFFF8624, 0x0000);  // WCR1
    m.cpu().regs().sr &= ~Cpu::kIMask;
    m.cpu().regs().r[1] = kMarker;
  }
  void poke32(u32 at, u32 v) {
    const u8 b[4] = {u8(v >> 24), u8(v >> 16), u8(v >> 8), u8(v)};
    m.bus().load(at, b, 4);
  }
  void code(u32 at, std::initializer_list<u16> words) {
    for (u16 w : words) { const u8 b[2] = {u8(w >> 8), u8(w)}; m.bus().load(at, b, 2); at += 2; }
  }
  Bus& bus() { return m.bus(); }
  u32 marker() { return bus().read32(kMarker); }
  u32 wait_vector(u64 max) {
    bus().write32(kMarker, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    return marker();
  }
};

void test_config_and_vectors() {
  System s;
  CHECK_EQ(s.m.config().rom_size, 0x40000u);
  CHECK_EQ(s.m.bus().read16(0x3FFFEu), 0xFFFFu);  // top of the on-chip ROM is mapped
  Intc& i = s.m.intc();
  CHECK_EQ(i.vector_of(IrqSrc::Irq4), 68u);
  CHECK_EQ(i.vector_of(IrqSrc::Dei2), 80u);
  CHECK_EQ(i.vector_of(IrqSrc::Tgi3a), 112u);
  CHECK_EQ(i.vector_of(IrqSrc::Tci4v), 124u);
  CHECK_EQ(i.vector_of(IrqSrc::Adi1), 137u);
  CHECK_EQ(i.vector_of(IrqSrc::Swdtend), 140u);
  CHECK_EQ(i.vector_of(IrqSrc::Oei), 156u);
  // IRQ5 through IPRB bits 11-8, edge sensing via ICR bit 2.
  s.bus().write16(0xFFFF834A, 0x0600);
  s.m.intc().set_irq_pin(5, true);
  CHECK_EQ(s.wait_vector(100), 69u);
  s.m.intc().set_irq_pin(5, false);
}

void test_dmac_four_channels() {
  System s;
  Bus& b = s.bus();
  for (u32 i = 0; i < 8; ++i) b.write8(System::kExtRam + i, u8(0xA0 + i));
  b.write16(0xFFFF834C, 0x0070);  // IPRC: DMAC2/3 level 7
  b.write32(0xFFFF86E0, System::kExtRam);          // SAR2
  b.write32(0xFFFF86E4, System::kExtRam + 0x100);  // DAR2
  b.write32(0xFFFF86E8, 0x01000008);               // DMATCR2: 24 bits wide, upper byte ignored
  CHECK_EQ(b.read32(0xFFFF86E8), 8u);
  b.write16(0xFFFF86B0, 0x0001);                   // DMAOR: DME
  b.write32(0xFFFF86EC, 0x00005425);               // CHCR2: SM/DM increment, auto-request, burst, byte, IE, DE
  CHECK_EQ(s.wait_vector(300), 80u);
  for (u32 i = 0; i < 8; ++i) CHECK_EQ(b.read8(System::kExtRam + 0x100 + i), u8(0xA0 + i));
  CHECK(b.read32(0xFFFF86EC) & 0x2);
}

void test_mtu_channels_3_and_4() {
  System s;
  Bus& b = s.bus();
  // Channel 3: clear on TGRA, phi/1, TGRA = TGRB = 3; flags at TSR3 (H'FFFF822C), counter wraps.
  b.write8(0xFFFF8208, 0x00);     // TIER3: poll
  b.write16(0xFFFF8218, 3);       // TGR3A
  b.write16(0xFFFF821A, 3);       // TGR3B
  b.write8(0xFFFF8200, 0x20);     // TCR3: clear on TGRA, phi/1
  b.write8(0xFFFF8240, 0x40);     // TSTR: CST3
  s.m.run(6);
  const u8 tsr3 = b.read8(0xFFFF822C);
  CHECK_EQ(tsr3 & 3, 3u);
  b.write8(0xFFFF822C, u8(tsr3 & ~1u));  // clear TGFA only
  CHECK_EQ(b.read8(0xFFFF822C) & 3, 2u);
  CHECK(b.read16(0xFFFF8210) < 4);
  // TGI3A interrupt (vector 112, IPRE bits 7-4).
  b.write16(0xFFFF8350, 0x0050);
  b.write8(0xFFFF8208, 0x01);     // TIER3: TGIEA
  CHECK_EQ(s.wait_vector(100), 112u);
  b.write8(0xFFFF8208, 0x00);     // TIER3 off: TGFA is still set
  // Channel 4 overflow at phi/4 (TPSC = 1): TCI4V vector 124, IPRF bits 11-8.
  b.write8(0xFFFF8240, 0x00);
  b.write16(0xFFFF8352, 0x0500);
  b.write8(0xFFFF8201, 0x01);     // TCR4: phi/4
  b.write16(0xFFFF8212, 0xFFF0);  // TCNT4
  b.write8(0xFFFF8209, 0x10);     // TIER4: TCIEV
  b.write8(0xFFFF8240, 0x80);     // TSTR: CST4
  CHECK_EQ(s.wait_vector(200), 124u);
  // phi/256 on channel 3 (TPSC = 4): 256 states per count.
  b.write8(0xFFFF8240, 0x00);
  b.write8(0xFFFF8200, 0x04);
  b.write16(0xFFFF8210, 0);
  b.write8(0xFFFF8240, 0x40);
  s.m.run(256 * 10 + 4);
  CHECK_EQ(b.read16(0xFFFF8210), 10u);
  // TGR3C / TGR3D exist; the stored-only registers keep their value.
  b.write16(0xFFFF8224, 0x1234);
  CHECK_EQ(b.read16(0xFFFF8224), 0x1234u);
  b.write8(0xFFFF820A, 0x3F);  // TOER
  CHECK_EQ(b.read8(0xFFFF820A), 0x3Fu);
}

void test_two_adc_units() {
  System s;
  Bus& b = s.bus();
  s.m.adc_mid_speed().set_input(3, 0x123);
  s.m.adc_mid_speed1().set_input(2, 0x2AB);
  b.write8(0xFFFF8410, 0x23);  // ADCSR0: ADST, CH3
  b.write8(0xFFFF8411, 0x22);  // ADCSR1: ADST, CH2
  s.m.run(300);
  CHECK(b.read8(0xFFFF8410) & 0x80);
  CHECK(b.read8(0xFFFF8411) & 0x80);
  CHECK_EQ(b.read16(0xFFFF8406), u16(0x123 << 6));  // ADDRD0
  CHECK_EQ(b.read16(0xFFFF840C), u16(0x2AB << 6));  // ADDRC1
  // ADI1 (vector 137) through IPRG bits 15-12.
  b.write16(0xFFFF8354, 0x4000);
  b.write8(0xFFFF8411, 0x62);  // ADIE, ADST
  CHECK_EQ(s.wait_vector(400), 137u & 0x7F);  // the marker handlers store the vector's low 7 bits
}

void test_ports() {
  System s;
  Bus& b = s.bus();
  Ports7042::Port port = Ports7042::Port::F; u32 value = 0, dir = 0;
  s.m.ports7042().set_output_hook([&](Ports7042::Port p, u32 v, u32 d) { port = p; value = v; dir = d; });
  b.write32(0xFFFF8384, 0x00FF00FF);  // PAIOR
  b.write32(0xFFFF8380, 0x00A5C33C);  // PADR
  CHECK(port == Ports7042::Port::A);
  CHECK_EQ(value, 0x00A5003Cu);
  CHECK_EQ(dir, 0x00FF00FFu);
  s.m.ports7042().set_input(Ports7042::Port::A, 0x00001100);
  CHECK_EQ(b.read32(0xFFFF8380), 0x00A5113Cu);
  b.write16(0xFFFF83B4, 0xFF00);  // PEIOR
  b.write16(0xFFFF83B0, 0x1234);  // PEDR
  s.m.ports7042().set_input(Ports7042::Port::E, 0x00FF);
  CHECK_EQ(b.read16(0xFFFF83B0), 0x12FFu);
  b.write8(0xFFFF8700, 0x81);     // DTEA
  CHECK_EQ(b.read8(0xFFFF8700), 0x81u);
  b.write16(0xFFFF838C, 0x5555);  // PACRL1 (stored)
  CHECK_EQ(b.read16(0xFFFF838C), 0x5555u);
}

}  // namespace

int main() {
  test_config_and_vectors();
  test_dmac_four_channels();
  test_mtu_channels_3_and_4();
  test_two_adc_units();
  test_ports();
  return test::finish("test_sh7042");
}

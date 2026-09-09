// SH7014 pin function controller / I/O ports (manual sections 16, 17) and the
// SH7017 flash-memory control registers (section 18.4/18.5).
#include <vector>

#include "cpu/sh2/machine.hpp"
#include "cpu/sh2/ports.hpp"
#include "common/test_util.hpp"

using namespace sh2;

namespace {

struct System {
  Machine m;
  Ports ports;
  FlashRegs flash;
  static constexpr u32 kRam = 0xFFFFF000u;
  System(ChipModel model, u8 mode) : m(model, mode), ports(m.config()), flash(m.config()) {
    m.bus().map_ram(0x00000000, 0x10000, Bus::kClsCs0);
    ports.map(m.io());
    flash.map(m.io());
    // Main program: NOP loop at on-chip RAM (only used by the instruction test).
    const u16 loop[] = {0x0009, 0xAFFD, 0x0009};
    u32 a = kRam;
    for (u16 w : loop) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    m.bus().write32(0, kRam);
    m.bus().write32(4, 0xFFFFFBF0u);
    m.cpu().invalidate_all();
    m.reset();
    ports.reset();
    flash.reset();
  }
  Bus& bus() { return m.bus(); }
};

// Registers common to every model, with their power-on values in mode 1.
struct RegCase { u32 addr; u16 reset; u16 mask14; u16 mask16; };
constexpr RegCase kCommon[] = {
    // addr             reset   SH7014 bits  SH7016/17 bits
    {Ports::kPadrl,   0x0000, 0x83FF, 0xFFFF},
    {Ports::kPaiorl,  0x0000, 0x83FF, 0xFFFF},
    {Ports::kPacrl1,  0x4000, 0x400F, 0x555F},
    {Ports::kPacrl2,  0x0000, 0xFD75, 0xFD75},
    {Ports::kPbdr,    0x0000, 0x03FC, 0x03FF},
    {Ports::kPbior,   0x0000, 0x03FC, 0x03FF},
    {Ports::kPbcr1,   0x0000, 0x000F, 0x000F},
    {Ports::kPbcr2,   0x0000, 0xFFF0, 0xFFF5},
    {Ports::kPedr,    0x0000, 0xFFFF, 0xFFFF},
    {Ports::kPeior,   0x0000, 0xFFFF, 0xFFFF},
    {Ports::kPecr1,   0x0000, 0xF000, 0xF000},
    {Ports::kPecr2,   0x0000, 0x55FF, 0x55FF},
};
constexpr u32 kPortCD[] = {Ports::kPcdr, Ports::kPcior, Ports::kPccr, Ports::kPddrl, Ports::kPdiorl, Ports::kPdcrl};

void check_reset_and_reserved(ChipModel model) {
  System s(model, 1);
  Bus& b = s.bus();
  const bool small = model == ChipModel::SH7014;
  // Inputs read the pin level: drive every pin low so a DR read shows only the latch.
  for (Port p : {Port::A, Port::B, Port::C, Port::D, Port::E}) s.ports.set_pins(p, 0x0000);
  for (const RegCase& r : kCommon) CHECK_EQ(b.read16(r.addr), r.reset);
  // All outputs first so that DR reads return the latch.
  for (u32 ior : {Ports::kPaiorl, Ports::kPbior, Ports::kPeior}) b.write16(ior, 0xFFFF);
  for (const RegCase& r : kCommon) {
    b.write16(r.addr, 0xFFFF);
    CHECK_EQ(b.read16(r.addr), small ? r.mask14 : r.mask16);
    b.write16(r.addr, 0x0000);
    CHECK_EQ(b.read16(r.addr), 0x0000);
  }
  // Byte halves of a 16-bit register.
  b.write16(Ports::kPecr2, 0xFFFF);
  CHECK_EQ(b.read8(Ports::kPecr2), 0x55);
  CHECK_EQ(b.read8(Ports::kPecr2 + 1), 0xFF);
  b.write8(Ports::kPecr2 + 1, 0x00);
  CHECK_EQ(b.read16(Ports::kPecr2), 0x5500);
  // 32-bit access covers two registers (PEDR/PFDR share a longword).
  s.ports.set_pins(Port::F, 0xA5);
  b.write16(Ports::kPeior, 0xFFFF);
  b.write16(Ports::kPedr, 0x1234);
  CHECK_EQ(b.read32(Ports::kPedr), 0x123400A5u);
  // Port C / D registers: absent on the SH7014 (unmapped: all ones, writes
  // ignored), ordinary registers on the SH7016/17.
  for (u32 a : kPortCD) {
    if (small) {
      CHECK_EQ(b.read16(a), 0xFFFF);
      b.write16(a, 0x1234);
      CHECK_EQ(b.read16(a), 0xFFFF);
      CHECK_EQ(b.read8(a), 0xFF);
    } else {
      CHECK_EQ(b.read16(a), 0x0000);
    }
  }
  if (!small) {
    b.write16(Ports::kPcior, 0xFFFF);
    b.write16(Ports::kPdiorl, 0xFFFF);
    for (u32 a : kPortCD) {
      b.write16(a, 0xFFFF);
      CHECK_EQ(b.read16(a), 0xFFFF);
    }
  }
  // Undecoded bytes between the registers read as unmapped register field.
  CHECK_EQ(b.read16(0xFFFF8384), 0xFFFF);
  CHECK_EQ(b.read16(0xFFFF83B6), 0xFFFF);
  CHECK_EQ(b.read16(0xFFFF839E), 0xFFFF);
}

void test_reset_values() {
  check_reset_and_reserved(ChipModel::SH7014);
  check_reset_and_reserved(ChipModel::SH7016);
  // PACRL1 resets to H'0000 in single-chip mode (table 16.4 note 1).
  System sc(ChipModel::SH7016, 3);
  CHECK_EQ(sc.bus().read16(Ports::kPacrl1), 0x0000);
  System m2(ChipModel::SH7017, 2);
  CHECK_EQ(m2.bus().read16(Ports::kPacrl1), 0x4000);
  // reset() restores the power-on values.
  m2.bus().write16(Ports::kPacrl1, 0x0000);
  m2.bus().write16(Ports::kPedr, 0xBEEF);
  m2.ports.reset();
  CHECK_EQ(m2.bus().read16(Ports::kPacrl1), 0x4000);
  CHECK_EQ(m2.ports.dr(Port::E), 0x0000);
}

void test_direction_and_readback() {
  System s(ChipModel::SH7014, 1);
  Bus& b = s.bus();
  // All inputs: the DR reads the host levels (masked to the existing pins).
  s.ports.set_pins(Port::A, 0xFFFF);
  CHECK_EQ(b.read16(Ports::kPadrl), 0x83FF);
  s.ports.set_pins(Port::A, 0x0F0F);
  CHECK_EQ(b.read16(Ports::kPadrl), 0x030F);  // PA11-PA10 do not exist on the SH7014
  // Low byte outputs: latch on outputs, pins on inputs.
  b.write16(Ports::kPaiorl, 0x00FF);
  b.write16(Ports::kPadrl, 0x5AA5);
  CHECK_EQ(b.read16(Ports::kPadrl), 0x03A5);
  CHECK_EQ(s.ports.dr(Port::A), 0x02A5);  // bits without a pin are not stored
  CHECK_EQ(s.ports.pin_levels(Port::A), 0x03A5);
  // Byte writes replace one half of the latch, not of the read value.
  b.write8(Ports::kPadrl + 1, 0x33);
  CHECK_EQ(b.read16(Ports::kPadrl), 0x0333);
  b.write8(Ports::kPadrl, 0xFF);
  CHECK_EQ(s.ports.dr(Port::A), 0x8333);
  CHECK_EQ(b.read16(Ports::kPadrl), 0x0333);  // upper half still inputs
  b.write16(Ports::kPaiorl, 0xFFFF);
  CHECK_EQ(b.read16(Ports::kPadrl), 0x8333);
  CHECK_EQ(s.ports.pin_levels(Port::A), 0x0333);  // PA15 is the CK output, not driven by the latch
  b.write16(Ports::kPacrl1, 0x0000);
  CHECK_EQ(s.ports.pin_levels(Port::A), 0x8333);
  // Writes to input pins land in the latch and show up once the pin becomes an output.
  b.write16(Ports::kPaiorl, 0x0000);
  b.write16(Ports::kPedr, 0xC3C3);
  s.ports.set_pins(Port::E, 0x0000);
  CHECK_EQ(b.read16(Ports::kPedr), 0x0000);
  b.write16(Ports::kPeior, 0xFFFF);
  CHECK_EQ(b.read16(Ports::kPedr), 0xC3C3);
  // Port B: only PB9-PB2 exist on the SH7014.
  s.ports.set_pins(Port::B, 0xFFFF);
  CHECK_EQ(b.read16(Ports::kPbdr), 0x03FC);
  b.write16(Ports::kPbior, 0x03FF);
  CHECK_EQ(b.read16(Ports::kPbior), 0x03FC);
  b.write16(Ports::kPbdr, 0x0003);
  CHECK_EQ(b.read16(Ports::kPbdr), 0x0000);
  // Port F: input only, 8 pins in the low byte, high byte reserved.
  s.ports.set_pins(Port::F, 0xFF5A);
  CHECK_EQ(b.read16(Ports::kPfdr), 0x005A);
  CHECK_EQ(b.read8(Ports::kPfdr + 1), 0x5A);
  CHECK_EQ(b.read8(Ports::kPfdr), 0x00);
  b.write16(Ports::kPfdr, 0xFFFF);
  b.write8(Ports::kPfdr + 1, 0xFF);
  CHECK_EQ(b.read16(Ports::kPfdr), 0x005A);
  CHECK_EQ(s.ports.pin_levels(Port::F), 0x005A);
}

void test_write_hook() {
  System s(ChipModel::SH7016, 1);
  Bus& b = s.bus();
  struct W { Port port; u16 dr, ior; };
  std::vector<W> writes;
  s.ports.set_write_hook([&](Port p, u16 dr, u16 ior) { writes.push_back({p, dr, ior}); });
  b.write16(Ports::kPeior, 0x00FF);
  b.write16(Ports::kPedr, 0x1234);
  b.write8(Ports::kPedr + 1, 0x56);
  b.write16(Ports::kPecr2, 0x0001);  // CR writes do not fire the hook
  b.write16(Ports::kPfdr, 0xFFFF);   // neither do ignored PFDR writes
  b.write16(Ports::kPddrl, 0xABCD);
  CHECK_EQ(writes.size(), 4u);
  CHECK(writes[0].port == Port::E);
  CHECK_EQ(writes[0].dr, 0x0000);
  CHECK_EQ(writes[0].ior, 0x00FF);
  CHECK_EQ(writes[1].dr, 0x1234);
  CHECK_EQ(writes[1].ior, 0x00FF);
  CHECK_EQ(writes[2].dr, 0x1256);
  CHECK(writes[3].port == Port::D);
  CHECK_EQ(writes[3].dr, 0xABCD);
  CHECK_EQ(writes[3].ior, 0x0000);
  // Nothing fires on the SH7014 for the absent port D.
  System t(ChipModel::SH7014, 1);
  int hits = 0;
  t.ports.set_write_hook([&](Port, u16, u16) { ++hits; });
  t.bus().write16(Ports::kPddrl, 0xABCD);
  CHECK_EQ(hits, 0);
}

void test_functions() {
  using F = PinFunction;
  // SH7014, mode 1: 11 port A pins, 8 port B pins, no port C / D.
  {
    System s(ChipModel::SH7014, 1);
    Bus& b = s.bus();
    CHECK(s.ports.function(Port::A, 15) == F::Ck);  // PACRL1 reset value
    CHECK(s.ports.function(Port::A, 14) == F::None);
    CHECK(s.ports.function(Port::A, 10) == F::None);
    CHECK(s.ports.function(Port::A, 9) == F::Gpio);
    CHECK(s.ports.function(Port::B, 1) == F::None);
    CHECK(s.ports.function(Port::C, 0) == F::None);
    CHECK(s.ports.function(Port::D, 15) == F::None);
    CHECK(s.ports.function(Port::F, 3) == F::Gpio);
    CHECK_EQ(s.ports.gpio_mask(Port::A), 0x03FF);
    CHECK_EQ(s.ports.gpio_mask(Port::B), 0x03FC);
    b.write16(Ports::kPacrl1, 0x000A);  // PA9 = IRQ3, PA8 = IRQ2 -> CK off
    CHECK(s.ports.function(Port::A, 15) == F::Gpio);
    CHECK(s.ports.function(Port::A, 9) == F::Irq3);
    CHECK(s.ports.function(Port::A, 8) == F::Irq2);
    b.write16(Ports::kPacrl1, 0x0005);
    CHECK(s.ports.function(Port::A, 9) == F::Tclkd);
    CHECK(s.ports.function(Port::A, 8) == F::Tclkc);
    b.write16(Ports::kPacrl1, 0x000F);
    CHECK(s.ports.function(Port::A, 9) == F::Reserved);
    // PACRL2: PA7 CS3, PA6 TCLKA, PA5 SCK1, PA4 TXD1, PA3 RXD1, PA2 DREQ0, PA1 TXD0, PA0 RXD0.
    b.write16(Ports::kPacrl2, 0x9565);
    CHECK(s.ports.function(Port::A, 7) == F::Cs3);
    CHECK(s.ports.function(Port::A, 6) == F::Tclka);
    CHECK(s.ports.function(Port::A, 5) == F::Sck1);
    CHECK(s.ports.function(Port::A, 4) == F::Txd1);
    CHECK(s.ports.function(Port::A, 3) == F::Rxd1);
    CHECK(s.ports.function(Port::A, 2) == F::Dreq0);
    CHECK(s.ports.function(Port::A, 1) == F::Txd0);
    CHECK(s.ports.function(Port::A, 0) == F::Rxd0);
    CHECK_EQ(s.ports.gpio_mask(Port::A), 0x8000);
    b.write16(Ports::kPacrl2, 0x0C30);
    CHECK(s.ports.function(Port::A, 5) == F::Irq1);
    CHECK(s.ports.function(Port::A, 2) == F::Irq0);
    CHECK_EQ(s.ports.cr2(Port::A), 0x0C30);
    // Port B: PB9 IRQ7 / A21, PB8 IRQ6 / A20 / WAIT, PB5 IRQ3 / RDWR, PB2 IRQ0 / RAS, PB7 A19.
    b.write16(Ports::kPbcr1, 0x0007);
    CHECK(s.ports.function(Port::B, 9) == F::Irq7);
    CHECK(s.ports.function(Port::B, 8) == F::Wait);
    b.write16(Ports::kPbcr1, 0x000A);
    CHECK(s.ports.function(Port::B, 9) == F::Address);
    CHECK(s.ports.function(Port::B, 8) == F::Address);
    b.write16(Ports::kPbcr2, 0x8C20);
    CHECK(s.ports.function(Port::B, 7) == F::Address);
    CHECK(s.ports.function(Port::B, 5) == F::Rdwr);
    CHECK(s.ports.function(Port::B, 2) == F::Reserved);
    b.write16(Ports::kPbcr2, 0x4830);
    CHECK(s.ports.function(Port::B, 7) == F::Reserved);
    CHECK(s.ports.function(Port::B, 5) == F::Reserved);
    CHECK(s.ports.function(Port::B, 2) == F::Ras);
    b.write16(Ports::kPbcr2, 0x0450);
    CHECK(s.ports.function(Port::B, 5) == F::Irq3);
    CHECK(s.ports.function(Port::B, 3) == F::Irq1);
    CHECK(s.ports.function(Port::B, 2) == F::Irq0);
    // Port E: PE15 DACK1, PE14 AH, PE7-4 TIOC, PE3 DRAK1, PE2 TIOC0C, PE1 DRAK0, PE0 TIOC0A.
    b.write16(Ports::kPecr1, 0xB000);
    CHECK(s.ports.function(Port::E, 15) == F::Dack1);
    CHECK(s.ports.function(Port::E, 14) == F::Ah);
    CHECK(s.ports.function(Port::E, 13) == F::Gpio);
    b.write16(Ports::kPecr1, 0x6000);
    CHECK(s.ports.function(Port::E, 15) == F::Reserved);
    CHECK(s.ports.function(Port::E, 14) == F::Dack0);
    b.write16(Ports::kPecr2, 0x5599);
    CHECK(s.ports.function(Port::E, 7) == F::Tioc2b);
    CHECK(s.ports.function(Port::E, 6) == F::Tioc2a);
    CHECK(s.ports.function(Port::E, 5) == F::Tioc1b);
    CHECK(s.ports.function(Port::E, 4) == F::Tioc1a);
    CHECK(s.ports.function(Port::E, 3) == F::Drak1);
    CHECK(s.ports.function(Port::E, 2) == F::Tioc0c);
    CHECK(s.ports.function(Port::E, 1) == F::Drak0);
    CHECK(s.ports.function(Port::E, 0) == F::Tioc0a);
    b.write16(Ports::kPecr2, 0x0063);
    CHECK(s.ports.function(Port::E, 3) == F::Tioc0d);
    CHECK(s.ports.function(Port::E, 2) == F::Dreq1);
    CHECK(s.ports.function(Port::E, 1) == F::Gpio);
    CHECK(s.ports.function(Port::E, 0) == F::Reserved);
    b.write16(Ports::kPecr2, 0x0002);
    CHECK(s.ports.function(Port::E, 0) == F::Dreq0);
    // pin_levels: a pin handed to a peripheral is not driven by the latch.
    b.write16(Ports::kPecr2, 0x0100);  // PE4 = TIOC1A
    b.write16(Ports::kPeior, 0x0030);  // PE5, PE4 outputs
    b.write16(Ports::kPedr, 0x0030);
    s.ports.set_pins(Port::E, 0x0000);
    CHECK_EQ(b.read16(Ports::kPedr), 0x0030);      // register view: latch on IOR = 1 pins
    CHECK_EQ(s.ports.pin_levels(Port::E), 0x0020);  // PE4 belongs to the MTU
  }
  // SH7016, mode 1: bus lines fixed regardless of the PFC.
  {
    System s(ChipModel::SH7016, 1);
    Bus& b = s.bus();
    CHECK(s.ports.function(Port::A, 14) == F::Rd);
    CHECK(s.ports.function(Port::A, 13) == F::Wrh);
    CHECK(s.ports.function(Port::A, 12) == F::Wrl);
    CHECK(s.ports.function(Port::A, 11) == F::Cs1);
    CHECK(s.ports.function(Port::A, 10) == F::Cs0);
    CHECK(s.ports.function(Port::B, 1) == F::Address);
    CHECK(s.ports.function(Port::B, 0) == F::Address);
    CHECK(s.ports.function(Port::C, 0) == F::Address);
    CHECK(s.ports.function(Port::C, 15) == F::Address);
    CHECK(s.ports.function(Port::D, 7) == F::Data);
    CHECK_EQ(s.ports.gpio_mask(Port::C), 0x0000);
    CHECK_EQ(s.ports.gpio_mask(Port::D), 0x0000);
    CHECK_EQ(s.ports.gpio_mask(Port::A), 0x03FF);
    b.write16(Ports::kPacrl1, 0x0000);
    CHECK(s.ports.function(Port::A, 14) == F::Rd);
    b.write16(Ports::kPccr, 0x0000);
    CHECK(s.ports.function(Port::C, 3) == F::Address);
  }
  // SH7016, mode 2 (ROM enabled): the same pins are ports until the PFC says otherwise.
  {
    System s(ChipModel::SH7016, 2);
    Bus& b = s.bus();
    CHECK(s.ports.function(Port::A, 14) == F::Gpio);
    CHECK(s.ports.function(Port::A, 10) == F::Gpio);
    CHECK(s.ports.function(Port::B, 0) == F::Gpio);
    CHECK(s.ports.function(Port::C, 9) == F::Gpio);
    CHECK(s.ports.function(Port::D, 9) == F::Gpio);
    b.write16(Ports::kPacrl1, 0x1550);
    CHECK(s.ports.function(Port::A, 14) == F::Rd);
    CHECK(s.ports.function(Port::A, 13) == F::Wrh);
    CHECK(s.ports.function(Port::A, 12) == F::Wrl);
    CHECK(s.ports.function(Port::A, 11) == F::Cs1);
    CHECK(s.ports.function(Port::A, 10) == F::Cs0);
    CHECK(s.ports.function(Port::A, 15) == F::Gpio);  // CK cleared
    b.write16(Ports::kPbcr2, 0x0005);
    CHECK(s.ports.function(Port::B, 1) == F::Address);
    CHECK(s.ports.function(Port::B, 0) == F::Address);
    b.write16(Ports::kPccr, 0x0200);
    CHECK(s.ports.function(Port::C, 9) == F::Address);
    CHECK(s.ports.function(Port::C, 8) == F::Gpio);
    b.write16(Ports::kPdcrl, 0x8001);
    CHECK(s.ports.function(Port::D, 15) == F::Data);
    CHECK(s.ports.function(Port::D, 0) == F::Data);
    CHECK(s.ports.function(Port::D, 1) == F::Gpio);
    CHECK_EQ(s.ports.gpio_mask(Port::D), 0x7FFE);
  }
  // SH7017, single-chip mode: bus / DMAC encodings fall back to general I/O.
  {
    System s(ChipModel::SH7017, 3);
    Bus& b = s.bus();
    CHECK(s.ports.function(Port::A, 15) == F::Gpio);  // PACRL1 = 0
    b.write16(Ports::kPacrl1, 0x555A);
    CHECK(s.ports.function(Port::A, 15) == F::Ck);  // CK is still selectable
    CHECK(s.ports.function(Port::A, 14) == F::Gpio);
    CHECK(s.ports.function(Port::A, 10) == F::Gpio);
    CHECK(s.ports.function(Port::A, 9) == F::Irq3);
    b.write16(Ports::kPacrl2, 0x8830);
    CHECK(s.ports.function(Port::A, 7) == F::Gpio);  // CS3
    CHECK(s.ports.function(Port::A, 5) == F::Gpio);  // DREQ1
    CHECK(s.ports.function(Port::A, 2) == F::Irq0);
    b.write16(Ports::kPbcr1, 0x000B);
    CHECK(s.ports.function(Port::B, 9) == F::Gpio);  // A21
    CHECK(s.ports.function(Port::B, 8) == F::Gpio);  // WAIT
    b.write16(Ports::kPbcr2, 0xBF05);
    CHECK(s.ports.function(Port::B, 7) == F::Gpio);  // A19
    CHECK(s.ports.function(Port::B, 5) == F::Gpio);  // RDWR
    CHECK(s.ports.function(Port::B, 4) == F::Gpio);  // CASH
    CHECK(s.ports.function(Port::B, 1) == F::Gpio);  // A17
    b.write16(Ports::kPccr, 0xFFFF);
    b.write16(Ports::kPdcrl, 0xFFFF);
    CHECK_EQ(s.ports.gpio_mask(Port::C), 0xFFFF);
    CHECK_EQ(s.ports.gpio_mask(Port::D), 0xFFFF);
    b.write16(Ports::kPecr1, 0xB000);
    CHECK(s.ports.function(Port::E, 15) == F::Gpio);  // DACK1
    CHECK(s.ports.function(Port::E, 14) == F::Gpio);  // AH
    b.write16(Ports::kPecr2, 0x00AA);
    CHECK(s.ports.function(Port::E, 3) == F::Gpio);  // DRAK1
    CHECK(s.ports.function(Port::E, 2) == F::Gpio);  // DREQ1
    b.write16(Ports::kPecr2, 0x0055);
    CHECK(s.ports.function(Port::E, 3) == F::Tioc0d);
    CHECK(s.ports.function(Port::E, 0) == F::Tioc0a);
  }
}

void test_through_instructions() {
  // MOV.W R0,@R1 ; MOV.W @R1,R2 ; MOV.W @R3,R4 ; NOP loop.
  System s(ChipModel::SH7014, 1);
  Bus& b = s.bus();
  const u16 code[] = {0x2101, 0x6211, 0x6431, 0x0009, 0xAFFD, 0x0009};
  u32 a = System::kRam;
  for (u16 w : code) { Bus::put_be16(b.ptr(a), w); a += 2; }
  s.m.cpu().invalidate_all();
  s.m.reset();
  Cpu::Regs& r = s.m.cpu().regs();
  r.pc = System::kRam;
  r.r[0] = 0x0000A5C3;
  r.r[1] = Ports::kPeior;
  r.r[3] = Ports::kPfdr;
  s.ports.set_pins(Port::F, 0x3C);
  s.m.run(40);
  CHECK_EQ(s.ports.ior(Port::E), 0xA5C3);
  CHECK_EQ(r.r[2], 0xFFFFA5C3u);  // MOV.W sign-extends
  CHECK_EQ(r.r[4], 0x0000003Cu);
}

void test_flash_regs() {
  // Absent on the SH7014 and SH7016: the addresses read as unmapped register field.
  for (ChipModel model : {ChipModel::SH7014, ChipModel::SH7016}) {
    System s(model, 1);
    CHECK(!s.flash.present());
    CHECK_EQ(s.bus().read8(FlashRegs::kFlmcr1), 0xFF);
    CHECK_EQ(s.bus().read8(FlashRegs::kFlmcr2), 0xFF);
    CHECK_EQ(s.bus().read8(FlashRegs::kEbr1), 0xFF);
    CHECK_EQ(s.bus().read16(FlashRegs::kRamer), 0xFFFF);
    s.bus().write16(FlashRegs::kRamer, 0x0007);
    CHECK_EQ(s.bus().read16(FlashRegs::kRamer), 0xFFFF);
  }
  // SH7017, mode 2 (flash enabled).
  {
    System s(ChipModel::SH7017, 2);
    Bus& b = s.bus();
    CHECK(s.flash.present());
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0x00);  // FWP low at power-on
    CHECK_EQ(b.read8(FlashRegs::kFlmcr2), 0x00);
    CHECK_EQ(b.read8(FlashRegs::kEbr1), 0x00);
    CHECK_EQ(b.read16(FlashRegs::kRamer), 0x0000);
    // FWE follows the pin and is read-only.
    b.write8(FlashRegs::kFlmcr1, 0xFF);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0x00);  // nothing writable while FWE = 0
    s.flash.set_fwe(true);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0x80);
    CHECK(s.flash.fwe());
    b.write8(FlashRegs::kFlmcr1, 0x00);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0x80);
    // Setting SWE together with lower bits only takes SWE.
    b.write8(FlashRegs::kFlmcr1, 0x7F);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0xC0);
    // Then PSU, then P (program sequence, 18.5.1).
    b.write8(FlashRegs::kFlmcr1, 0x50);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0xD0);
    b.write8(FlashRegs::kFlmcr1, 0x52);  // E without ESU is refused, P written as 0
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0xD0);
    b.write8(FlashRegs::kFlmcr1, 0x51);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0xD1);
    CHECK_EQ(s.flash.flmcr1(), 0xD1);
    // Erase sequence: SWE, ESU, E; EV / PV need SWE only.
    b.write8(FlashRegs::kFlmcr1, 0x50);
    b.write8(FlashRegs::kFlmcr1, 0x40);
    b.write8(FlashRegs::kFlmcr1, 0x60);
    b.write8(FlashRegs::kFlmcr1, 0x62);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0xE2);
    b.write8(FlashRegs::kFlmcr1, 0x60);
    b.write8(FlashRegs::kFlmcr1, 0x48);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0xC8);
    b.write8(FlashRegs::kFlmcr1, 0x44);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0xC4);
    b.write8(FlashRegs::kFlmcr1, 0x40);
    // EBR1 is writable only with SWE set and is cleared when SWE drops.
    b.write8(FlashRegs::kEbr1, 0x10);
    CHECK_EQ(b.read8(FlashRegs::kEbr1), 0x10);
    CHECK_EQ(s.flash.ebr1(), 0x10);
    b.write8(FlashRegs::kFlmcr1, 0x00);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0x80);
    CHECK_EQ(b.read8(FlashRegs::kEbr1), 0x00);
    b.write8(FlashRegs::kEbr1, 0x01);
    CHECK_EQ(b.read8(FlashRegs::kEbr1), 0x00);
    // FWP low initialises FLMCR1 and EBR1 (table 18.8).
    b.write8(FlashRegs::kFlmcr1, 0x40);
    b.write8(FlashRegs::kEbr1, 0x80);
    CHECK_EQ(b.read8(FlashRegs::kEbr1), 0x80);
    s.flash.set_fwe(false);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0x00);
    CHECK_EQ(b.read8(FlashRegs::kEbr1), 0x00);
    s.flash.set_fwe(true);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0x80);
    // FLMCR2: FLER read-only, reserved bits 0.
    b.write8(FlashRegs::kFlmcr2, 0xFF);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr2), 0x00);
    // RAMER: RAMS, RAM1-0 only; 8- and 16-bit access.
    b.write16(FlashRegs::kRamer, 0xFFFF);
    CHECK_EQ(b.read16(FlashRegs::kRamer), 0x0007);
    CHECK(s.flash.ram_emulation());
    CHECK_EQ(s.flash.ram_block(), 3u);
    CHECK_EQ(s.flash.ram_overlay_base(), 0x0001FC00u);
    b.write8(FlashRegs::kRamer + 1, 0x05);
    CHECK_EQ(b.read16(FlashRegs::kRamer), 0x0005);
    CHECK_EQ(s.flash.ram_overlay_base(), 0x0001F400u);
    b.write8(FlashRegs::kRamer, 0xFF);
    CHECK_EQ(b.read16(FlashRegs::kRamer), 0x0005);
    CHECK_EQ(b.read8(FlashRegs::kRamer), 0x00);
    b.write16(FlashRegs::kRamer, 0x0000);
    CHECK(!s.flash.ram_emulation());
    // Reset clears everything but FWE.
    b.write8(FlashRegs::kFlmcr1, 0x40);
    b.write16(FlashRegs::kRamer, 0x0004);
    s.flash.reset();
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0x80);
    CHECK_EQ(b.read16(FlashRegs::kRamer), 0x0000);
  }
  // SH7017, mode 1 (ROM disabled): flash registers read H'00 and ignore writes.
  {
    System s(ChipModel::SH7017, 1);
    Bus& b = s.bus();
    s.flash.set_fwe(true);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0x00);
    b.write8(FlashRegs::kFlmcr1, 0x40);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr1), 0x00);
    CHECK_EQ(b.read8(FlashRegs::kEbr1), 0x00);
    CHECK_EQ(b.read8(FlashRegs::kFlmcr2), 0x00);
    b.write16(FlashRegs::kRamer, 0x0006);
    CHECK_EQ(b.read16(FlashRegs::kRamer), 0x0006);
  }
}

}  // namespace

int main() {
  test_reset_values();
  test_direction_and_readback();
  test_write_hook();
  test_functions();
  test_through_instructions();
  test_flash_regs();
  return test::finish("test_sh2_ports");
}

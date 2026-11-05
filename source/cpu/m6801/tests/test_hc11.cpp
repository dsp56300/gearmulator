// MC68HC11 tests: reset state, the prebyte pages and the Y register, the bit
// instructions, IDIV / FDIV, the nine-byte interrupt frame, XIRQ and the X
// bit, the illegal-opcode trap, the timer (prescaler, output compares and
// their pins, overflow, RTI), the pulse accumulator, HPRIO, the SCI, the A/D,
// the register block's fall-through, the MC6850 ACIA and the disassembly.
#include <initializer_list>
#include <vector>

#include "common/test_util.hpp"
#include "cpu/m6801/disasm.hpp"
#include "cpu/m6801/hc11machine.hpp"
#include "cpu/m6801/mc6850.hpp"

using namespace m6801;

namespace {

constexpr u16 kCode = 0xC000, kStack = 0x7F00, kHandler = 0xD000, kIllopHandler = 0xD200, kXirqHandler = 0xD300;

bool near(u64 a, u64 b) { return a + 4 >= b && a <= b + 4; }

struct System {
  Hc11Machine m;
  System() {
    m.map_ram(0x2000, 0x6000);
    m.map_rom(0x8000, 0x8000);
    vector(hc11::kVecReset, kCode);
    vector(hc11::kVecIllop, kIllopHandler);
    vector(hc11::kVecXirq, kXirqHandler);
    code({0x3B}, kIllopHandler);  // RTI
    code({0x3B}, kXirqHandler);
    m.reset();
    m.cpu().set_sp(kStack);
  }
  Cpu& cpu() { return m.cpu(); }
  Hc11Peripherals& p() { return m.periph(); }
  u8* mem() { return m.bus().mem(); }
  void vector(u16 at, u16 target) { mem()[at] = u8(target >> 8); mem()[at + 1] = u8(target); }
  void code(std::initializer_list<u8> bytes, u16 at = kCode) {
    u16 a = at;
    for (u8 b : bytes) mem()[a++] = b;
    cpu().invalidate_range(at, u32(bytes.size()));
  }
  u64 steps(unsigned n) {
    u64 st = 0;
    for (unsigned i = 0; i < n; ++i) st += m.step();
    return st;
  }
  void run_until(u64 t) {
    if (m.now() < t) m.run(t - m.now());
  }
  u8 reg(u16 offset) { return m.bus().read8(hc11::kRegBase + offset); }
};

void test_reset() {
  System s;
  CHECK_EQ(s.cpu().pc(), kCode);
  CHECK_EQ(s.cpu().ccr(), u8(kS | kX | kI));
  CHECK_EQ(s.reg(hc11::kHprio), 0x25);
  CHECK_EQ(s.reg(hc11::kScsr), 0xC0);
  CHECK_EQ(s.reg(hc11::kConfig), 0x0C);
  CHECK_EQ(s.reg(hc11::kOption), 0x10);
  // The internal RAM answers at $0000-$00FF.
  s.m.bus().write8(0x0042, 0x5A);
  CHECK_EQ(s.m.bus().read8(0x0042), 0x5A);
}

void test_y_and_pages() {
  System s;
  s.code({
      0x18, 0xCE, 0x12, 0x34,  // LDY #$1234        4
      0xCE, 0x20, 0x00,        // LDX #$2000        3
      0x1A, 0xEF, 0x02,        // STY 2,X           6
      0x18, 0xCE, 0x20, 0x00,  // LDY #$2000        4
      0xCD, 0xEE, 0x02,        // LDX 2,Y           6
      0xCC, 0x12, 0x34,        // LDD #$1234        3
      0x1A, 0x83, 0x12, 0x34,  // CPD #$1234 -> Z   5
      0xCD, 0xA3, 0x02,        // CPD 2,Y -> Z      7
      0x18, 0x8F,              // XGDY              4
      0x18, 0x08,              // INY               4
      0x18, 0x3C,              // PSHY              5
      0x18, 0x38,              // PULY              6
      0xC6, 0x10,              // LDAB #$10         2
      0x18, 0x3A,              // ABY               4
      0x18, 0x8C, 0x20, 0x11,  // CPY #$2011 -> Z   5
      0x18, 0xA6, 0x02,        // LDAA 2,Y          5
  });
  CHECK_EQ(s.steps(1), 4u);
  CHECK_EQ(s.cpu().y(), 0x1234);
  s.steps(1);
  CHECK_EQ(s.steps(1), 6u);
  CHECK_EQ(s.mem()[0x2002], 0x12);
  CHECK_EQ(s.mem()[0x2003], 0x34);
  s.steps(1);
  CHECK_EQ(s.steps(1), 6u);
  CHECK_EQ(s.cpu().x(), 0x1234);
  s.steps(1);
  CHECK_EQ(s.steps(1), 5u);
  CHECK(s.cpu().ccr() & kZ);
  CHECK_EQ(s.steps(1), 7u);
  CHECK(s.cpu().ccr() & kZ);
  CHECK_EQ(s.steps(1), 4u);
  CHECK_EQ(s.cpu().y(), 0x1234);
  CHECK_EQ(s.cpu().d(), 0x2000);
  s.steps(1);
  CHECK_EQ(s.cpu().y(), 0x1235);
  CHECK_EQ(s.steps(2), 11u);
  CHECK_EQ(s.cpu().y(), 0x1235);
  CHECK_EQ(s.cpu().sp(), kStack);
  s.cpu().set_y(0x2001);
  s.steps(2);
  CHECK_EQ(s.cpu().y(), 0x2011);
  s.steps(1);
  CHECK(s.cpu().ccr() & kZ);
  s.mem()[0x2013] = 0x99;
  CHECK_EQ(s.steps(1), 5u);
  CHECK_EQ(s.cpu().a(), 0x99);

  // An opcode outside a page traps.
  System t;
  t.code({0x18, 0x86, 0x00});
  t.steps(1);
  CHECK_EQ(t.cpu().pc(), kIllopHandler);
  CHECK_EQ(t.cpu().illegal_instructions(), 1u);
}

void test_bit_instructions() {
  System s;
  s.mem()[0x0040] = 0x81;
  s.code({
      0x14, 0x40, 0x06,              // BSET $40,#$06 -> 87          6
      0xCE, 0x00, 0x40,              // LDX #$0040
      0x1D, 0x00, 0x80,              // BCLR 0,X,#$80 -> 07          7
      0x12, 0x40, 0x07, 0x02,        // BRSET $40,#$07,+2 (taken)    6
      0x01, 0x01,                    // (skipped)
      0x13, 0x40, 0x01, 0x10,        // BRCLR $40,#$01,+16 (not)     6
      0x18, 0xCE, 0x00, 0x40,        // LDY #$0040
      0x18, 0x1F, 0x00, 0x80, 0x10,  // BRCLR 0,Y,#$80,+16 (taken)   8
  });
  CHECK_EQ(s.steps(1), 6u);
  CHECK_EQ(s.mem()[0x0040], 0x87);
  CHECK(s.cpu().ccr() & kN);
  s.steps(1);
  CHECK_EQ(s.steps(1), 7u);
  CHECK_EQ(s.mem()[0x0040], 0x07);
  CHECK_EQ(s.steps(1), 6u);
  CHECK_EQ(s.cpu().pc(), kCode + 15);
  CHECK_EQ(s.steps(1), 6u);
  CHECK_EQ(s.cpu().pc(), kCode + 19);
  s.steps(1);
  CHECK_EQ(s.steps(1), 8u);
  CHECK_EQ(s.cpu().pc(), kCode + 28 + 16);
}

void test_divide() {
  System s;
  s.code({
      0xCC, 0x03, 0xE8,  // LDD #1000
      0xCE, 0x00, 0x07,  // LDX #7
      0x02,              // IDIV -> X 142, D 6     41
      0xCC, 0x40, 0x00,  // LDD #$4000
      0xCE, 0x80, 0x00,  // LDX #$8000
      0x03,              // FDIV -> X $8000, D 0   41
      0xCE, 0x00, 0x00,  // LDX #0
      0x02,              // IDIV by zero -> C
      0xCC, 0x80, 0x00,  // LDD #$8000
      0xCE, 0x40, 0x00,  // LDX #$4000
      0x03,              // FDIV with X <= D -> V
  });
  s.steps(2);
  CHECK_EQ(s.steps(1), 41u);
  CHECK_EQ(s.cpu().x(), 142);
  CHECK_EQ(s.cpu().d(), 6);
  s.steps(2);
  CHECK_EQ(s.steps(1), 41u);
  CHECK_EQ(s.cpu().x(), 0x8000);
  CHECK_EQ(s.cpu().d(), 0);
  CHECK(!(s.cpu().ccr() & (kV | kC)));
  s.steps(2);
  CHECK(s.cpu().ccr() & kC);
  CHECK_EQ(s.cpu().x(), 0xFFFF);
  s.steps(3);
  CHECK(s.cpu().ccr() & kV);
  CHECK(!(s.cpu().ccr() & kC));
}

// SWI stacks PC, Y, X, A, B, CC (nine bytes, low byte first); RTI restores
// them, but cannot set X again once it is clear.
void test_interrupt_frame() {
  System s;
  s.vector(hc11::kVecSwi, kHandler);
  s.code({0x86, 0x00, 0x06, 0x3B}, kHandler);  // LDAA #0; TAP; RTI
  s.code({0xCC, 0x12, 0x34, 0xCE, 0x56, 0x78, 0x18, 0xCE, 0x9A, 0xBC, 0x3F, 0x01});
  s.steps(3);
  CHECK_EQ(s.steps(1), 14u);
  CHECK_EQ(s.cpu().pc(), kHandler);
  CHECK_EQ(s.cpu().sp(), kStack - 9);
  const u8* st = s.mem() + kStack - 8;
  CHECK_EQ(st[0] & 0xF0, kS | kX | kI);  // CC
  CHECK_EQ(st[1], 0x34);              // B
  CHECK_EQ(st[2], 0x12);              // A
  CHECK_EQ(st[3], 0x56);              // XH
  CHECK_EQ(st[4], 0x78);
  CHECK_EQ(st[5], 0x9A);  // YH
  CHECK_EQ(st[6], 0xBC);
  CHECK_EQ(st[7], 0xC0);  // PCH
  CHECK_EQ(st[8], 0x0B);
  s.steps(2);  // LDAA #0, TAP: X and S clear
  CHECK_EQ(s.cpu().ccr() & (kX | kS), 0);
  CHECK_EQ(s.steps(1), 12u);  // RTI: X stays clear, S and I come back
  CHECK_EQ(s.cpu().ccr() & (kX | kS | kI), kS | kI);
  CHECK_EQ(s.cpu().y(), 0x9ABC);
  CHECK_EQ(s.cpu().pc(), kCode + 11);
}

// XIRQ waits for X to clear; its entry sets X and I.
void test_xirq() {
  System s;
  s.code({0x20, 0xFE}, kXirqHandler);
  s.code({0x01, 0x01, 0x86, 0x00, 0x06, 0x01, 0x01});  // NOP NOP LDAA #0 TAP NOP NOP
  s.p().set_xirq(true);
  s.steps(2);
  CHECK_EQ(s.cpu().interrupts_taken(), 0u);
  s.steps(2);
  s.steps(1);
  CHECK_EQ(s.cpu().interrupts_taken(), 1u);
  CHECK_EQ(s.cpu().pc(), kXirqHandler + 0);
  CHECK_EQ(s.cpu().ccr() & (kX | kI), kX | kI);
}

void test_illegal_opcode() {
  System s;
  s.code({0x01, 0x41});
  s.steps(1);
  CHECK_EQ(s.steps(1), 14u);
  CHECK_EQ(s.cpu().pc(), kIllopHandler);
  // The stacked return address is the illegal opcode's.
  CHECK_EQ(s.mem()[kStack], 0x01);
  CHECK_EQ(s.mem()[kStack - 1], 0xC0);
}

// A tempo clock: OC1 every 1000 counts with the /4 prescaler.
void test_output_compare_interrupt() {
  System s;
  s.vector(hc11::kVecOc1, kHandler);
  s.code({0x86, 0x80, 0xB7, 0x10, 0x23,  // LDAA #$80; STAA TFLG1
          0xFC, 0x10, 0x16, 0xC3, 0x03, 0xE8, 0xFD, 0x10, 0x16,  // LDD TOC1; ADDD #1000; STD TOC1
          0x7C, 0x00, 0x50, 0x3B},  // INC $50; RTI
         kHandler);
  s.code({0x86, 0x01, 0xB7, 0x10, 0x24,  // LDAA #$01; STAA TMSK2 (PR = /4)
          0xCC, 0x03, 0xE8, 0xFD, 0x10, 0x16,  // LDD #1000; STD TOC1
          0x86, 0x80, 0xB7, 0x10, 0x22,  // LDAA #$80; STAA TMSK1
          0x0E, 0x20, 0xFE});            // CLI; BRA *
  s.steps(2);
  CHECK_EQ(s.reg(hc11::kTmsk2) & 3, 1);
  s.run_until(4000 * 10 + 3000);
  // The first match at count 1000 (cycle 4000), then every 4000 cycles.
  CHECK_EQ(s.mem()[0x50], 10);
  CHECK(s.p().counter() >= 10750 && s.p().counter() <= 10760);
}

// TOF after 65536 counts, RTIF every 8192 E cycles (RTR = 00).
void test_overflow_and_rti() {
  System s;
  s.code({0x20, 0xFE});
  s.run_until(8192 - 2);
  CHECK(!(s.p().timer_flags2() & 0x40));
  s.run_until(8192 + 4);
  CHECK(s.p().timer_flags2() & 0x40);
  s.run_until(65536 - 2);
  CHECK(!(s.p().timer_flags2() & 0x80));
  s.run_until(65536 + 4);
  CHECK(s.p().timer_flags2() & 0x80);
}

// OC2 toggles PA6 at every match; OC1 drives PA3 from OC1D.
void test_output_compare_pins() {
  System s;
  std::vector<std::pair<u64, u8>> changes;
  s.p().set_port_write_hook(Hc11Peripherals::kPortA, [&](u8 d, u8) { changes.push_back({s.m.now(), d}); });
  s.code({0x86, 0x40, 0xB7, 0x10, 0x20,  // LDAA #$40; STAA TCTL1 (OM2:OL2 = 01, toggle)
          0xCC, 0x01, 0x00, 0xFD, 0x10, 0x18,  // LDD #$0100; STD TOC2
          0x86, 0x08, 0xB7, 0x10, 0x0C, 0xB7, 0x10, 0x0D,  // OC1M = OC1D = $08
          0xCC, 0x02, 0x00, 0xFD, 0x10, 0x16,  // LDD #$0200; STD TOC1
          0x20, 0xFE});
  s.run_until(0x10000 + 0x100 + 10);
  // PA6 at $100, PA3 at $200, PA6 again a full period later.
  CHECK_EQ(changes.size(), 3u);
  if (changes.size() == 3) {
    CHECK_EQ(changes[0].second & 0x48, 0x40);
    CHECK(changes[0].first >= 0x100 && changes[0].first <= 0x104);
    CHECK_EQ(changes[1].second & 0x48, 0x48);
    CHECK_EQ(changes[2].second & 0x48, 0x08);
  }
  CHECK_EQ(s.reg(hc11::kPorta) & 0x48, 0x08);
}

// Event counting on PA7's falling edges, PAIF interrupt.
void test_pulse_accumulator() {
  System s;
  s.vector(hc11::kVecPai, kHandler);
  s.code({0x86, 0x10, 0xB7, 0x10, 0x25, 0x7C, 0x00, 0x50, 0x3B}, kHandler);  // clear PAIF; INC $50; RTI
  s.code({0x86, 0x40, 0xB7, 0x10, 0x26,   // PACTL: PAEN, falling edges
          0x86, 0x10, 0xB7, 0x10, 0x24,   // TMSK2: PAII
          0x0E, 0x20, 0xFE});
  s.steps(5);
  for (int i = 0; i < 3; ++i) {
    s.p().set_port_input(Hc11Peripherals::kPortA, 0x7F);
    s.run_until(s.m.now() + 100);
    s.p().set_port_input(Hc11Peripherals::kPortA, 0xFF);
    s.run_until(s.m.now() + 100);
  }
  CHECK_EQ(s.mem()[0x50], 3);
  CHECK_EQ(s.reg(hc11::kPacnt), 3);
}

// PSEL lifts a source over the fixed order: IRQ normally wins over OC2.
void test_hprio() {
  for (const u8 psel : {u8(0x05), u8(0x0C)}) {
    System s;
    s.vector(hc11::kVecIrq, kHandler);
    s.vector(hc11::kVecOc2, kHandler + 0x10);
    s.code({0x20, 0xFE}, kHandler);
    s.code({0x20, 0xFE}, kHandler + 0x10);
    s.code({0x86, psel, 0xB7, 0x10, 0x3C,  // HPRIO
            0x86, 0x40, 0xB7, 0x10, 0x22,  // TMSK1: OC2I
            0x86, 0x40, 0xB7, 0x10, 0x23,  // TFLG1: clear OC2F
            0xCC, 0x02, 0x00, 0xFD, 0x10, 0x18,  // TOC2 = $200
            0x20, 0xFE});
    s.steps(7);
    s.p().set_irq(true);
    s.run_until(0x300);
    CHECK_EQ(s.cpu().interrupts_taken(), 0u);  // I is still set
    s.cpu().set_ccr(u8(s.cpu().ccr() & ~kI));
    s.steps(1);
    CHECK_EQ(s.cpu().pc(), psel == 0x0C ? kHandler + 0x10 : kHandler);
  }
}

// TIE-driven transmission at 31250 baud (BAUD = $20 at E = 2 MHz: 64 E per bit).
void test_sci_transmit() {
  System s;
  std::vector<std::pair<u64, u8>> out;
  s.p().set_tx_hook([&](u8 b) { out.push_back({s.m.now(), b}); });
  s.vector(hc11::kVecSci, kHandler);
  // Handler: LDAA SCSR; LDX $50; LDAA 0,X; STAA SCDR; INX; STX $50; CPX #$2003; BNE +3; CLR SCCR2... (TIE off)
  s.code({0xB6, 0x10, 0x2E, 0xDE, 0x50, 0xA6, 0x00, 0xB7, 0x10, 0x2F, 0x08, 0xDF, 0x50,
          0x8C, 0x20, 0x03, 0x26, 0x05, 0x86, 0x08, 0xB7, 0x10, 0x2D, 0x3B},
         kHandler);
  s.mem()[0x2000] = 0xF0;
  s.mem()[0x2001] = 0x41;
  s.mem()[0x2002] = 0x7F;
  s.m.bus().write8(0x50, 0x20);
  s.m.bus().write8(0x51, 0x00);
  s.code({0x86, 0x20, 0xB7, 0x10, 0x2B,  // BAUD = $20
          0x86, 0x88, 0xB7, 0x10, 0x2D,  // SCCR2 = TIE | TE
          0x0E, 0x20, 0xFE});
  s.run_until(10000);
  CHECK_EQ(s.p().sci_bit_cycles(), 64u);
  CHECK_EQ(out.size(), 3u);
  if (out.size() == 3) {
    CHECK_EQ(out[0].second, 0xF0);
    CHECK_EQ(out[1].second, 0x41);
    CHECK_EQ(out[2].second, 0x7F);
    // Back to back: one frame (640 E) apart, seen at the next instruction boundary.
    CHECK(near(out[1].first - out[0].first, 640));
    CHECK(near(out[2].first - out[1].first, 640));
  }
  CHECK(s.reg(hc11::kScsr) & 0x40);  // TC: idle
}

void test_adc() {
  System s;
  for (unsigned ch = 0; ch < 8; ++ch) s.p().set_analog_input(ch, u8(0x10 * ch + 1));
  s.code({0x86, 0x14, 0xB7, 0x10, 0x30, 0x20, 0xFE});  // ADCTL = MULT, group 4-7
  s.steps(2);
  CHECK(!(s.reg(hc11::kAdctl) & 0x80));
  s.run_until(s.m.now() + 130);
  CHECK(s.reg(hc11::kAdctl) & 0x80);
  CHECK_EQ(s.reg(hc11::kAdr1), 0x41);
  CHECK_EQ(s.reg(hc11::kAdr1 + 3), 0x71);
}

// The bytes of the register line the chip leaves alone reach the board.
void test_register_fallthrough() {
  struct Probe final : emu::Device {
    u32 last_write = 0;
    u8 read8(u32 a) override { return u8(a); }
    void write8(u32 a, u8) override { last_write = a; }
  } probe;
  System s;
  s.m.map_device(0x1000, 0x80, &probe);
  CHECK_EQ(s.m.bus().read8(0x1004), 0x04);  // PORTB: external in the expanded mode
  CHECK_EQ(s.m.bus().read8(0x1045), 0x45);
  s.m.bus().write8(0x1050, 1);
  CHECK_EQ(probe.last_write, 0x1050u);
  CHECK_EQ(s.m.bus().read8(0x1039), 0x10);  // OPTION stays internal
}

// A MIDI ACIA: 2 MHz clock on E, divide by 64, 8N1.
void test_acia() {
  System s;
  Mc6850 acia(s.m.sched(), s.cpu());
  std::vector<std::pair<u64, u8>> out;
  bool irq = false;
  acia.set_tx_hook([&](u8 b) { out.push_back({s.m.now(), b}); });
  acia.set_irq_hook([&](bool on) { irq = on; s.p().set_xirq(on); });
  s.m.map_device(0x1400, 0x80, &acia);
  s.code({0x86, 0x03, 0xB7, 0x14, 0x00,  // master reset
          0x86, 0x96, 0xB7, 0x14, 0x00,  // RIE, 8N1, /64
          0x86, 0x90, 0xB7, 0x14, 0x01,  // TDR = $90
          0x20, 0xFE});
  s.steps(6);
  CHECK(!(acia.status() & Mc6850::kTdre));
  s.run_until(s.m.now() + 100);  // in the shifter now
  CHECK(acia.status() & Mc6850::kTdre);
  s.m.bus().write8(0x1401, 0x3C);
  s.run_until(s.m.now() + 2000);
  CHECK_EQ(out.size(), 2u);
  if (out.size() == 2) {
    CHECK_EQ(out[0].second, 0x90);
    CHECK_EQ(out[1].second, 0x3C);
    CHECK(near(out[1].first - out[0].first, 640));
  }
  // Receive: RDRF and IRQ after a frame; reading the data clears both.
  const u64 t = s.m.now();
  acia.receive(0x55);
  s.run_until(t + 600);
  CHECK(!irq);
  s.run_until(t + 700);
  CHECK(irq);
  CHECK_EQ(s.m.bus().read8(0x1401), 0x55);
  CHECK(!irq);
}

std::string dis(std::initializer_list<u8> bytes, u16 pc = 0xC000) {
  std::vector<u8> b(bytes);
  b.resize(8);
  return disassemble(decode(Family::HC11, pc, [&](unsigned i) { return b[i]; }));
}

void test_disassembler() {
  CHECK_EQ(dis({0x18, 0xCE, 0x12, 0x34}), std::string("LDY #$1234"));
  CHECK_EQ(dis({0x1A, 0x83, 0x00, 0x10}), std::string("CPD #$0010"));
  CHECK_EQ(dis({0xCD, 0xA3, 0x10}), std::string("CPD $10,Y"));
  CHECK_EQ(dis({0x18, 0x08}), std::string("INY"));
  CHECK_EQ(dis({0x14, 0x56, 0x10}), std::string("BSET $56,#$10"));
  CHECK_EQ(dis({0x1E, 0x00, 0x04, 0x1C}), std::string("BRSET $00,X,#$04,$C020"));
  CHECK_EQ(dis({0x18, 0x1F, 0x05, 0x80, 0x02}), std::string("BRCLR $05,Y,#$80,$C007"));
  CHECK_EQ(dis({0x02}), std::string("IDIV"));
  CHECK_EQ(dis({0x00}), std::string("TRAP"));
}

}  // namespace

int main() {
  test_reset();
  test_y_and_pages();
  test_bit_instructions();
  test_divide();
  test_interrupt_frame();
  test_xirq();
  test_illegal_opcode();
  test_output_compare_interrupt();
  test_overflow_and_rti();
  test_output_compare_pins();
  test_pulse_accumulator();
  test_hprio();
  test_sci_transmit();
  test_adc();
  test_register_fallthrough();
  test_acia();
  test_disassembler();
  return test::finish("m6801_test_hc11");
}

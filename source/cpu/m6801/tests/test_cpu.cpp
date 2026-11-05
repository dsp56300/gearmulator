// CPU tests: reset, arithmetic and the HD6301 additions, timing per family,
// the interrupt sequence and its hold-off rules, WAI, SLP, NMI, TRAP, the
// instruction cache over RAM, and the bus overlay (mode-dependent register
// bytes, the internal RAM and RAME).
#include <initializer_list>
#include <vector>

#include "common/test_util.hpp"
#include "cpu/m6801/disasm.hpp"
#include "cpu/m6801/machine.hpp"

using namespace m6801;

namespace {

constexpr u16 kCode = 0xC000, kIrqHandler = 0xD000, kNmiHandler = 0xD100, kTrapHandler = 0xD200, kStack = 0x7F00;

struct System {
  Machine m;
  explicit System(ChipModel model = ChipModel::HD6303R, int mode = 2) : m(model, mode) {
    m.map_ram(0x0000, 0x8000);  // external RAM, including what the chip leaves of $0000-$00FF
    m.map_rom(0x8000, 0x8000);
    vector(kVecReset, kCode);
    vector(kVecIrq1, kIrqHandler);
    vector(kVecNmi, kNmiHandler);
    vector(kVecTrap, kTrapHandler);
    code({0x3B}, kIrqHandler);  // RTI
    code({0x3B}, kNmiHandler);
    code({0x3B}, kTrapHandler);
    m.reset();
    m.cpu().set_sp(kStack);
  }
  Cpu& cpu() { return m.cpu(); }
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
  u8 flags() { return cpu().ccr() & 0x2F; }
};

void test_reset() {
  for (ChipModel model : {ChipModel::MC6803, ChipModel::HD6303R}) {
    System s(model, 2);
    CHECK_EQ(s.cpu().pc(), kCode);
    CHECK_EQ(s.cpu().ccr(), u8(kCcrFixed | kI));
    CHECK_EQ(s.m.bus().read8(kRamCtrl), 0x7F);  // RAME set, STBY PWR clear after power-up
    CHECK_EQ(s.m.bus().read8(kP2Data) >> 5, 2);  // the latched mode
    CHECK_EQ(s.m.bus().read8(kTrcsr), 0x20);
  }
}

void test_alu_flags() {
  System s;
  s.code({
      0x86, 0x7F,  // LDAA #$7F
      0x8B, 0x01,  // ADDA #$01  -> 80: N V H
      0x80, 0x81,  // SUBA #$81  -> FF: N C
      0x86, 0x09,  // LDAA #$09
      0x8B, 0x01,  // ADDA #$01  -> 0A
      0x19,        // DAA        -> 10
      0x86, 0x12,  // LDAA #$12
      0xC6, 0x34,  // LDAB #$34
      0x3D,        // MUL        -> 03A8, C = bit 7
      0xCC, 0x80, 0x00,  // LDD #$8000
      0x83, 0x00, 0x01,  // SUBD #1 -> 7FFF: V
      0x05,        // ASLD -> FFFE: N V
  });
  s.steps(2);
  CHECK_EQ(s.cpu().a(), 0x80);
  CHECK_EQ(s.flags(), kN | kV | kH);
  s.steps(1);
  CHECK_EQ(s.cpu().a(), 0xFF);
  CHECK_EQ(s.flags(), kN | kC | kH);  // SUB leaves H alone
  s.steps(3);
  CHECK_EQ(s.cpu().a(), 0x10);
  s.steps(3);
  CHECK_EQ(s.cpu().d(), 0x03A8);
  CHECK(s.cpu().ccr() & kC);
  s.steps(2);
  CHECK_EQ(s.cpu().d(), 0x7FFF);
  CHECK_EQ(s.flags() & (kN | kZ | kV | kC), kV);
  s.steps(1);
  CHECK_EQ(s.cpu().d(), 0xFFFE);
  CHECK_EQ(s.flags() & (kN | kZ | kV | kC), kN | kV);
}

void test_hd6301_instructions() {
  System s;
  s.mem()[0x0040] = 0x5A;
  s.code({
      0x71, 0x0F, 0x40,  // AIM #$0F,$40 -> 0A
      0x72, 0xC0, 0x40,  // OIM #$C0,$40 -> CA
      0x75, 0xFF, 0x40,  // EIM #$FF,$40 -> 35
      0x7B, 0x80, 0x40,  // TIM #$80,$40 -> Z
      0xCE, 0x12, 0x34,  // LDX #$1234
      0xCC, 0xAB, 0xCD,  // LDD #$ABCD
      0x18,              // XGDX
  });
  s.steps(1);
  CHECK_EQ(s.mem()[0x40], 0x0A);
  s.steps(1);
  CHECK_EQ(s.mem()[0x40], 0xCA);
  CHECK(s.cpu().ccr() & kN);
  s.steps(1);
  CHECK_EQ(s.mem()[0x40], 0x35);
  s.steps(1);
  CHECK_EQ(s.mem()[0x40], 0x35);
  CHECK(s.cpu().ccr() & kZ);
  s.steps(3);
  CHECK_EQ(s.cpu().x(), 0xABCD);
  CHECK_EQ(s.cpu().d(), 0x1234);

  // On the MC6803 $18 is undefined: a no-op, counted.
  System t(ChipModel::MC6803, 2);
  t.code({0xCE, 0x12, 0x34, 0x18, 0x01});
  t.steps(2);
  CHECK_EQ(t.cpu().x(), 0x1234);
  CHECK_EQ(t.cpu().pc(), kCode + 4);
  CHECK_EQ(t.cpu().illegal_instructions(), 1u);
}

// The same instructions take the MC6801's or the HD6301's cycles.
void test_cycle_counts() {
  const std::initializer_list<u8> prog = {
      0x86, 0x01,        // LDAA #1      2 / 2
      0x97, 0x90,        // STAA $90     3 / 3
      0xDC, 0x90,        // LDD $90      4 / 4
      0xA6, 0x00,        // LDAA 0,X     4 / 4
      0xB6, 0x00, 0x90,  // LDAA $0090   4 / 4
      0x08,              // INX          3 / 1
      0x36,              // PSHA         3 / 4
      0x32,              // PULA         4 / 3
      0x3C,              // PSHX         4 / 5
      0x38,              // PULX         5 / 4
      0x3D,              // MUL         10 / 7
      0x6C, 0x00,        // INC 0,X      6 / 6
      0x7C, 0x00, 0x90,  // INC $0090    6 / 6
      0x6F, 0x00,        // CLR 0,X      6 / 5
      0x8D, 0x02,        // BSR sub      6 / 5
      0x20, 0x01,        // BRA over     3 / 3
      0x39,              // sub: RTS     5 / 5
  };
  const unsigned n = 17;
  System a(ChipModel::MC6803, 2), b(ChipModel::HD6303R, 2);
  a.code(prog);
  b.code(prog);
  a.cpu().set_x(0x0090);
  b.cpu().set_x(0x0090);
  CHECK_EQ(a.steps(n), u64(2 + 3 + 4 + 4 + 4 + 3 + 3 + 4 + 4 + 5 + 10 + 6 + 6 + 6 + 6 + 5 + 3));
  CHECK_EQ(b.steps(n), u64(2 + 3 + 4 + 4 + 4 + 1 + 4 + 3 + 5 + 4 + 7 + 6 + 6 + 5 + 5 + 5 + 3));
}

void test_irq_and_rti() {
  System s;
  s.code({0x0E, 0x20, 0xFE});  // CLI; BRA *
  s.code({0x4C, 0x3B}, kIrqHandler);  // INCA; RTI (the request stays: level)
  s.steps(2);
  s.m.periph().set_irq1(true);
  s.steps(1);
  CHECK_EQ(s.cpu().interrupts_taken(), 1u);
  CHECK_EQ(s.cpu().a(), 1);  // the entry and the first handler instruction
  CHECK(s.cpu().ccr() & kI);
  CHECK_EQ(s.cpu().sp(), u16(kStack - 7));
  CHECK_EQ(s.mem()[kStack - 6], u8(kCcrFixed));  // the stacked CC, I clear
  CHECK_EQ(s.mem()[kStack], u8(kCode + 1));      // PC low, then high
  s.m.periph().set_irq1(false);
  s.steps(1);  // RTI
  CHECK_EQ(s.cpu().pc(), kCode + 1);
  CHECK_EQ(s.cpu().sp(), kStack);
  CHECK(!(s.cpu().ccr() & kI));
}

// A request is accepted two cycles after the instruction that cleared I.
void test_cli_holdoff() {
  for (int nops = 1; nops <= 2; ++nops) {
    System s;  // HD6303R: NOP is one cycle
    s.code({0x0E, 0x01, 0x01, 0x0F, 0x20, 0xFE});  // CLI; NOP; NOP; SEI; BRA *
    if (nops == 1) s.code({0x0E, 0x01, 0x0F, 0x20, 0xFE});
    s.m.periph().set_irq1(true);
    s.steps(unsigned(nops + 2));
    CHECK_EQ(s.cpu().interrupts_taken(), nops == 2 ? 1u : 0u);
  }
  {
    System s(ChipModel::MC6803, 2);  // NOP is two cycles: one instruction is enough
    s.code({0x0E, 0x01, 0x0F, 0x20, 0xFE});
    s.m.periph().set_irq1(true);
    s.steps(3);
    CHECK_EQ(s.cpu().interrupts_taken(), 1u);
  }
  for (ChipModel model : {ChipModel::MC6803, ChipModel::HD6303R}) {
    // MC6801 RM 5.4.1.2: CLRA; TAP; TAP; TAP; SEI never admits an interrupt.
    System s(model, 2);
    s.code({0x4F, 0x06, 0x06, 0x06, 0x0F, 0x20, 0xFE});
    s.m.periph().set_irq1(true);
    s.steps(6);
    CHECK_EQ(s.cpu().interrupts_taken(), 0u);
    // CLI; WAI: the request is serviced out of WAI, not before it.
    System w(model, 2);
    w.code({0x0E, 0x3E, 0x20, 0xFE});
    w.m.periph().set_irq1(true);
    w.steps(2);
    CHECK(w.cpu().waiting());
    CHECK_EQ(w.cpu().interrupts_taken(), 0u);
    w.steps(1);
    CHECK_EQ(w.cpu().interrupts_taken(), 1u);
    CHECK_EQ(w.mem()[kStack], u8(kCode + 2));  // stacked by WAI: return after it
  }
}

void test_nmi_edge() {
  System s;
  s.code({0x20, 0xFE});
  s.code({0x7C, 0x00, 0x40, 0x3B}, kNmiHandler);  // INC $0040; RTI
  s.m.periph().set_nmi(true);
  s.steps(1);
  CHECK(s.cpu().ccr() & kI);  // taken regardless of the mask, which it sets
  s.steps(2);
  CHECK_EQ(s.mem()[0x40], 1);
  s.m.periph().set_nmi(true);  // still low: no new edge
  s.steps(4);
  CHECK_EQ(s.mem()[0x40], 1);
  s.m.periph().set_nmi(false);
  s.m.periph().set_nmi(true);
  s.steps(3);
  CHECK_EQ(s.mem()[0x40], 2);
}

void test_wai() {
  System s;
  s.code({0x0E, 0x3E, 0x4C, 0x20, 0xFE});  // CLI; WAI; INCA; BRA *
  s.code({0x5C, 0x3B}, kIrqHandler);       // INCB; RTI
  s.steps(2);
  CHECK(s.cpu().waiting());
  CHECK_EQ(s.cpu().sp(), u16(kStack - 7));
  const u64 t0 = s.m.now();
  s.m.run(100);
  CHECK(s.cpu().waiting());
  CHECK(s.m.now() >= t0 + 100);
  s.m.periph().set_irq1(true);
  const u64 entry = s.m.step();  // the vector fetch and INCB
  CHECK_EQ(entry, u64(kWaiExitCycles + 1));
  CHECK(!s.cpu().waiting());
  CHECK_EQ(s.cpu().b(), 1);
  s.m.periph().set_irq1(false);
  s.steps(2);  // RTI; INCA
  CHECK_EQ(s.cpu().a(), 1);
  CHECK_EQ(s.cpu().sp(), kStack);
}

void test_slp() {
  System s;
  s.code({0x1A, 0x4C, 0x0E, 0x1A, 0x4C, 0x20, 0xFE});  // SLP; INCA; CLI; SLP; INCA; BRA *
  s.code({0x5C, 0x0F, 0x3B}, kIrqHandler);            // INCB; SEI; RTI
  s.steps(1);
  CHECK(s.cpu().sleeping());
  s.m.run(50);
  CHECK(s.cpu().sleeping());
  // A masked request ends the sleep without being serviced.
  s.m.periph().set_irq1(true);
  s.steps(1);
  CHECK(!s.cpu().sleeping());
  CHECK_EQ(s.cpu().a(), 1);
  CHECK_EQ(s.cpu().b(), 0);
  s.m.periph().set_irq1(false);
  s.steps(2);  // CLI; SLP
  CHECK(s.cpu().sleeping());
  s.m.periph().set_irq1(true);
  s.steps(1);  // the entry and INCB
  CHECK_EQ(s.cpu().b(), 1);
  s.m.periph().set_irq1(false);
  s.steps(3);  // SEI; RTI; INCA
  CHECK_EQ(s.cpu().a(), 2);
}

void test_trap() {
  System s;
  s.code({0x00});  // undefined on the HD6301
  s.steps(1);
  CHECK_EQ(s.cpu().pc(), u16(kTrapHandler));
  CHECK_EQ(s.mem()[kStack], u8(kCode));  // the faulting address: RTI retries it
  CHECK(s.cpu().ccr() & kI);
  CHECK_EQ(s.cpu().illegal_instructions(), 1u);
  // Address error: an instruction fetched from the register field.
  System t;
  t.code({0x7E, 0x00, 0x05});  // JMP $0005
  const u64 st = t.steps(2);
  CHECK_EQ(st, u64(3 + 12));
  CHECK_EQ(t.cpu().pc(), u16(kTrapHandler));
  CHECK_EQ(t.mem()[kStack], 0x05);
  CHECK_EQ(t.mem()[kStack - 1], 0x00);
}

// Code in RAM is re-decoded after a store into it: the internal RAM line,
// and the external RAM behind the register field's line.
void test_self_modifying_code() {
  for (u16 at : {u16(0x0090), u16(0x0040)}) {
    System s;
    s.mem()[at] = 0x86;  // LDAA #$11
    s.mem()[at + 1] = 0x11;
    s.mem()[at + 2] = 0x39;  // RTS
    s.code({
        0xBD, u8(at >> 8), u8(at),         // JSR at
        0xC6, 0x22,                        // LDAB #$22
        0xF7, u8((at + 1) >> 8), u8(at + 1),  // STAB at+1
        0xBD, u8(at >> 8), u8(at),         // JSR at
    });
    s.steps(3);
    CHECK_EQ(s.cpu().a(), 0x11);
    s.steps(5);
    CHECK_EQ(s.cpu().a(), 0x22);
  }
}

// Which register-field bytes are the chip's depends on the mode.
void test_mode_register_decoding() {
  struct Probe final : emu::Device {
    u8 read8(u32 a) override { last = a; return 0x5A; }
    void write8(u32 a, u8) override { last = a; }
    u32 last = 0xFFFF;
  };
  {
    System s(ChipModel::HD6303R, 2);
    Probe p;
    s.m.map_device(0x0000, 0x80, &p);
    CHECK_EQ(s.m.bus().read8(kP3Data), 0x5A);  // port 3 is the bus
    CHECK_EQ(p.last, u32(kP3Data));
    CHECK_EQ(s.m.bus().read8(kP1Ddr), 0xFF);   // port 1 is the chip's (write-only DDR)
    CHECK_EQ(s.m.bus().read8(0x0030), 0x5A);
  }
  {
    System s(ChipModel::HD6303R, 1);  // non-multiplexed: port 1 is the low address bus
    Probe p;
    s.m.map_device(0x0000, 0x80, &p);
    CHECK_EQ(s.m.bus().read8(kP1Data), 0x5A);
    CHECK_EQ(p.last, u32(kP1Data));
    CHECK_EQ(s.m.bus().read8(kP2Data) >> 5, 1);
  }
  {
    System s(ChipModel::MC6803, 3);  // no internal RAM
    s.mem()[0x00A0] = 0;
    s.m.bus().write8(0x00A0, 0x77);
    CHECK_EQ(s.m.bus().read8(0x00A0), 0x77);
    CHECK_EQ(s.m.bus().line_kind(0x00A0), Bus::Line::Ram);
  }
}

// A data access to a device happens at its bus cycle within the
// instruction (MC6801 RM appendix F, HD6301V1 manual table 3-3-1).
void test_access_cycles() {
  struct Probe final : emu::Device {
    explicit Probe(Machine& m) : m(m) {}
    u8 read8(u32) override { log.push_back(int(m.now() - start)); return 0; }
    void write8(u32, u8) override { log.push_back(100 + int(m.now() - start)); }
    Machine& m;
    u64 start = 0;
    std::vector<int> log;
  };
  struct Case {
    std::initializer_list<u8> code;
    std::vector<int> mc6801, hd6301;  // read cycles, 100 + write cycles
  };
  const Case cases[] = {
      {{0x96, 0x04}, {2}, {1}},                          // LDAA $04
      {{0xB6, 0x00, 0x04}, {3}, {2}},                    // LDAA $0004
      {{0xA6, 0x04}, {3}, {2}},                          // LDAA 4,X
      {{0x97, 0x04}, {102}, {101}},                      // STAA $04
      {{0xDC, 0x04}, {2, 3}, {1, 2}},                    // LDD $04
      {{0xFD, 0x00, 0x04}, {103, 104}, {102, 103}},      // STD $0004
      {{0x7C, 0x00, 0x04}, {3, 105}, {2, 104}},          // INC $0004
      {{0x6F, 0x04}, {3, 105}, {2, 103}},                // CLR 4,X
      {{0x7D, 0x00, 0x04}, {3}, {2}},                    // TST $0004
  };
  for (ChipModel model : {ChipModel::MC6803, ChipModel::HD6303R}) {
    for (const Case& c : cases) {
      System s(model, 2);  // $04-$07 are external in the expanded modes
      Probe p(s.m);
      s.m.map_device(0x0000, 0x80, &p);
      s.code(c.code);
      s.cpu().set_x(0);
      p.start = s.m.now();
      s.steps(1);
      CHECK(p.log == (model == ChipModel::MC6803 ? c.mc6801 : c.hd6301));
    }
  }
  // HD6301 bit operations: read, then write two cycles later.
  System s;
  Probe p(s.m);
  s.m.map_device(0x0000, 0x80, &p);
  s.code({0x71, 0x0F, 0x04, 0x6B, 0x01, 0x04});  // AIM #$0F,$04; TIM #$01,4,X
  s.cpu().set_x(0);
  p.start = s.m.now();
  s.steps(1);
  CHECK(p.log == std::vector<int>({2, 104}));
  p.log.clear();
  p.start = s.m.now();
  s.steps(1);
  CHECK(p.log == std::vector<int>({3}));
}

// RAME moves $0080-$00FF to the external bus; the internal RAM keeps its data.
void test_ram_enable() {
  System s;
  s.code({
      0x86, 0x11, 0x97, 0x90,  // LDAA #$11; STAA $90   (internal)
      0x7F, 0x00, 0x14,        // CLR $0014             RAME off
      0x86, 0x22, 0x97, 0x90,  // LDAA #$22; STAA $90   (external)
      0x86, 0x40, 0x97, 0x14,  // LDAA #$40; STAA $14   RAME on
      0x96, 0x90,              // LDAA $90
  });
  s.steps(3);
  CHECK(!s.m.periph().ram_enabled());
  CHECK_EQ(s.m.bus().read8(0x0090), 0x00);  // the external RAM, as mapped
  s.steps(4);
  CHECK(s.m.periph().ram_enabled());
  s.steps(1);
  CHECK_EQ(s.cpu().a(), 0x11);
  CHECK_EQ(s.m.bus().read8(kRamCtrl), 0x7F);
}

void test_disassembler() {
  auto dis = [](Family f, u16 pc, std::initializer_list<u8> bytes) {
    const u8* b = bytes.begin();
    return disassemble(decode(f, pc, [b](unsigned i) { return b[i]; }));
  };
  CHECK_EQ(dis(Family::M6801, 0, {0x86, 0x12}), std::string("LDAA #$12"));
  CHECK_EQ(dis(Family::M6801, 0, {0xE7, 0x34}), std::string("STAB $34,X"));
  CHECK_EQ(dis(Family::M6801, 0, {0xCC, 0x12, 0x34}), std::string("LDD #$1234"));
  CHECK_EQ(dis(Family::M6801, 0, {0xBD, 0x12, 0x34}), std::string("JSR $1234"));
  CHECK_EQ(dis(Family::M6801, 0xC000, {0x26, 0x0E}), std::string("BNE $C010"));
  CHECK_EQ(dis(Family::M6801, 0, {0x50}), std::string("NEGB"));
  CHECK_EQ(dis(Family::M6801, 0, {0x36}), std::string("PSHA"));
  CHECK_EQ(dis(Family::M6801, 0, {0x6F, 0x05}), std::string("CLR $05,X"));
  CHECK_EQ(dis(Family::HD6301, 0, {0x71, 0x0F, 0x12}), std::string("AIM #$0F,$12"));
  CHECK_EQ(dis(Family::HD6301, 0, {0x6B, 0x80, 0x03}), std::string("TIM #$80,$03,X"));
  CHECK_EQ(dis(Family::HD6301, 0, {0x18}), std::string("XGDX"));
  CHECK_EQ(dis(Family::HD6301, 0, {0x00}), std::string("TRAP"));
  CHECK_EQ(format_bytes(std::initializer_list<u8>{0xCE, 0x12, 0x34}.begin(), 3), std::string("CE 12 34"));
}

}  // namespace

int main() {
  test_reset();
  test_alu_flags();
  test_hd6301_instructions();
  test_cycle_counts();
  test_irq_and_rti();
  test_cli_holdoff();
  test_nmi_edge();
  test_wai();
  test_slp();
  test_trap();
  test_self_modifying_code();
  test_mode_register_decoding();
  test_access_cycles();
  test_ram_enable();
  test_disassembler();
  return test::finish("m6801_test_cpu");
}

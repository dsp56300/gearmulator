// Peripheral tests: the free-running counter's write and read protocols,
// output compare and overflow interrupts at their cycle, the compare output
// on P21, input capture, the IRQ2 flip-flop's spurious SCI vector, the SCI
// transmitter and receiver timing and flag protocols, wake-up, and the
// ports.
#include <initializer_list>
#include <vector>

#include "common/test_util.hpp"
#include "cpu/m6801/machine.hpp"

using namespace m6801;

namespace {

constexpr u16 kCode = 0xC000, kStack = 0x7F00;

struct System {
  Machine m;
  explicit System(ChipModel model = ChipModel::HD6303R) : m(model, 2) {
    m.map_ram(0x0000, 0x8000);
    m.map_rom(0x8000, 0x8000);
    vector(kVecReset, kCode);
    m.reset();
    m.cpu().set_sp(kStack);
  }
  Cpu& cpu() { return m.cpu(); }
  Peripherals& p() { return m.periph(); }
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
  u8 reg(u16 a) { return m.bus().read8(a); }
  void reg(u16 a, u8 v) { m.bus().write8(a, v); }
  void run_until(u64 t) { if (m.now() < t) m.run(t - m.now()); }
};

// MC6801 RM 7.2.1: STX $09 presets $FFF8; a double-byte read right after it
// returns $FFFB. The low byte comes from the buffer the high-byte read fills.
void test_counter_mc6801() {
  System s(ChipModel::MC6803);
  s.code({0xDF, 0x09, 0xDE, 0x09, 0x96, 0x0A, 0x01, 0x01, 0xD6, 0x0A});  // STX $09; LDX $09; LDAA $0A; NOP; NOP; LDAB $0A
  s.cpu().set_x(0x1234);
  s.steps(2);
  CHECK_EQ(s.cpu().x(), 0xFFFB);
  s.steps(4);
  CHECK_EQ(s.cpu().a(), 0xFB);
  CHECK_EQ(s.cpu().b(), 0xFB);  // no new high-byte read, the same low byte
}

// HD6301V1 Q&A: the high-byte write presets $FFF8, the low-byte write that
// follows loads the whole value (LDD #$5AF3; STD $09 -> $FFF8, $5AF3, $5AF4...).
void test_counter_hd6301() {
  System s;
  s.code({0xCC, 0x5A, 0xF3, 0xDD, 0x09, 0xDC, 0x09});  // LDD #$5AF3; STD $09; LDD $09
  s.steps(3);
  CHECK_EQ(s.cpu().d(), 0x5AF5);  // STD writes the low byte in its third cycle, LDD reads in its second
  // A write of the high byte alone presets $FFF8.
  System t;
  t.code({0x97, 0x09, 0xDC, 0x09});  // STAA $09; LDD $09
  t.steps(2);
  CHECK_EQ(t.cpu().d(), 0xFFF8 + 2);
}

// The compare interrupt is taken at the first boundary after the match is
// visible; the handler reads the counter at a known cycle.
void test_output_compare_interrupt() {
  for (ChipModel model : {ChipModel::HD6303R, ChipModel::MC6803}) {
    System s(model);
    const u16 handler = 0xD000;
    s.vector(kVecOcf, handler);
    s.code({
        0xCC, 0x01, 0x00,  // LDD #$0100
        0xDD, 0x0B,        // STD $0B   OCR
        0x86, 0x08,        // LDAA #$08
        0x97, 0x08,        // STAA $08  EOCI
        0x0E,              // CLI
        0x20, 0xFE,        // BRA *
    });
    s.code({0xDC, 0x09, 0xDD, 0x40, 0x20, 0xFE}, handler);  // LDD $09; STD $40; BRA *
    s.run_until(400);
    // HD6301: the BRA spanning the match ends at 259, LDD reads at entry + 12 + 1.
    // MC6801: the BRA ends at 257, LDD reads at entry + 12 + 2.
    const u16 expect = model == ChipModel::HD6303R ? 259 + 12 + 1 : 257 + 12 + 2;
    CHECK_EQ(u16((s.mem()[0x40] << 8) | s.mem()[0x41]), expect);
    CHECK(s.p().timer_status() & 0x40);
    CHECK_EQ(s.cpu().interrupts_taken(), 1u);
  }
}

// TOF: the MC6801 sets it while the counter holds $FFFF, the HD6301 once it
// reaches $0000; visible the cycle after.
void test_overflow_flag() {
  for (ChipModel model : {ChipModel::MC6803, ChipModel::HD6303R}) {
    System s(model);
    s.code({0x20, 0xFE});
    const u64 visible = model == ChipModel::MC6803 ? 0x10000 : 0x10001;
    s.run_until(0xFFF0);
    while (s.m.now() < 0x10010) {
      const bool tof = (s.p().timer_status() & 0x20) != 0;
      CHECK_EQ(tof, s.m.now() >= visible);
      s.m.step();
    }
  }
  // Cleared by a TCSR read with TOF set, then a read of the counter high byte.
  System s;
  s.code({0x96, 0x09, 0x96, 0x08, 0x96, 0x09, 0x20, 0xFE});  // LDAA $09; LDAA $08; LDAA $09
  s.run_until(0x10010);
  s.cpu().set_pc(kCode);
  s.steps(1);
  CHECK(s.p().timer_status() & 0x20);  // a counter read alone does not clear it
  s.steps(2);
  CHECK(!(s.p().timer_status() & 0x20));
}

// The compare output: OLVL is clocked to P21 on the match.
void test_output_compare_pin() {
  System s;
  std::vector<u8> levels;
  s.p().set_port_write_hook(2, [&](u8 data, u8 ddr) { if (ddr & 0x02) levels.push_back(data & 0x02); });
  s.code({0x20, 0xFE});
  s.reg(kP2Ddr, 0x02);
  s.reg(kOcrH, 0x02);
  s.reg(kOcrL, 0x00);
  s.reg(kTcsr, 0x01);  // OLVL
  CHECK(!s.p().output_level());
  s.run_until(0x1F0);
  CHECK(!s.p().output_level());
  s.run_until(0x210);
  CHECK(s.p().output_level());
  CHECK(!levels.empty() && levels.back() == 0x02);
  CHECK_EQ(s.p().port_output(2) & 0x02, 0x02);
}

void test_input_capture() {
  System s;
  s.code({0x20, 0xFE});
  s.run_until(1000);
  const u64 t = s.m.now();
  s.p().set_port_input(2, 0xFE);  // P20 falls: IEDG = 0 captures
  CHECK_EQ(s.p().input_capture(), u16(t + 1));
  CHECK(s.p().timer_status() & 0x80);
  s.reg(kTcsr, 0x02);  // rising edge now
  s.run_until(2000);
  s.p().set_port_input(2, 0xFE);  // no change
  s.p().set_port_input(2, 0xFF);  // rises
  CHECK_EQ(s.p().input_capture(), u16(s.m.now() + 1));
  // ICF clears on a TCSR read, then a read of ICR high.
  CHECK(s.reg(kTcsr) & 0x80);
  s.reg(kIcrH);
  CHECK(!(s.reg(kTcsr) & 0x80));
}

// MC6801 RM figure 5-13: TOF set and cleared within one LDD $08 while I is
// clear leaves the IRQ2 flip-flop set with no source: the SCI vector.
void test_spurious_sci_vector() {
  for (bool clear_in_flight : {true, false}) {
    System s(ChipModel::MC6803);
    const u16 sci = 0xD000, tof = 0xD100;
    s.vector(kVecSci, sci);
    s.vector(kVecTof, tof);
    s.code({0x7C, 0x00, 0x41, 0x20, 0xFE}, sci);  // INC $41; BRA *
    s.code({0x7C, 0x00, 0x42, 0x20, 0xFE}, tof);  // INC $42; BRA *
    // The counter is preset to $FFF8 in cycle 9 and holds $FFFF in cycle 17; the
    // second LDD $08 reads TCSR in cycle 18 and the counter in cycle 19.
    s.code({
        0x86, 0x04,  // LDAA #$04
        0x97, 0x08,  // STAA $08   ETOI
        0x0E,        // CLI
        0x97, 0x09,  // STAA $09   counter = $FFF8
        0x01,        // NOP
        0xDC, 0x08,  // LDD $08
        u8(clear_in_flight ? 0xDC : 0x96), 0x08,  // LDD $08 / LDAA $08
        0x20, 0xFE,
    });
    s.run_until(100);
    CHECK_EQ(s.mem()[0x41], clear_in_flight ? 1 : 0);
    CHECK_EQ(s.mem()[0x42], clear_in_flight ? 0 : 1);
  }
}

// HD6303R at E/128: TE starts a ten-bit preamble at the next bit-clock edge
// (the counter's bit 7), the byte is loaded at its end and leaves at the end
// of its stop bit.
void test_sci_transmit() {
  System s;
  std::vector<std::pair<u64, u8>> sent;
  s.p().set_tx_hook([&](u8 b) { sent.push_back({s.m.now(), b}); });
  s.code({
      0x86, 0x05, 0x97, 0x10,  // RMCR: NRZ, internal clock, E/128
      0x86, 0x02, 0x97, 0x11,  // TRCSR: TE (written in cycle 8: preamble from 128 to 1408)
      0x96, 0x11,              // LDAA $11  (TDRE set: armed)
      0x86, 0x41, 0x97, 0x13,  // STAA $13  (TDRE clears, the byte waits for the preamble)
      0x20, 0xFE,
  });
  s.steps(7);
  CHECK(!(s.reg(kTrcsr) & 0x20));
  s.run_until(1400);
  CHECK(!(s.p().sci_status() & 0x20));
  s.run_until(1500);
  CHECK(s.p().sci_status() & 0x20);  // loaded at 1408: TDR empty again
  CHECK(sent.empty());
  s.run_until(3000);
  CHECK_EQ(sent.size(), size_t(1));
  if (sent.size() == 1) {
    CHECK_EQ(sent[0].first, u64(1408 + 1280));
    CHECK_EQ(sent[0].second, 0x41);
  }
  // Without the TRCSR read the write only replaces TDR: nothing is sent.
  s.reg(kTdr, 0x55);
  s.run_until(6000);
  CHECK_EQ(sent.size(), size_t(1));
  // Back to back: the next byte loads at the end of the previous frame.
  s.reg(kTrcsr);
  s.reg(kTdr, 0x01);
  const u64 t = s.m.now();
  s.run_until(t + 300);
  s.reg(kTrcsr);
  s.reg(kTdr, 0x02);
  s.run_until(t + 4000);
  CHECK_EQ(sent.size(), size_t(3));
  // (the hook sees the boundary after the frame end: within one BRA)
  if (sent.size() == 3) CHECK(sent[2].first - sent[1].first >= 1278 && sent[2].first - sent[1].first <= 1282);
}

// The MC6801's preamble is nine ones.
void test_sci_transmit_mc6801() {
  System s(ChipModel::MC6803);
  std::vector<u64> when;
  s.p().set_tx_hook([&](u8) { when.push_back(s.m.now()); });
  s.code({0x86, 0x05, 0x97, 0x10, 0x86, 0x02, 0x97, 0x11, 0x96, 0x11, 0x86, 0x41, 0x97, 0x13, 0x20, 0xFE});
  s.run_until(4000);
  CHECK_EQ(when.size(), size_t(1));
  // TE written in cycle 9: preamble 128..1280, frame to 2560 (seen at the next boundary).
  if (!when.empty()) CHECK(when[0] >= 2560 && when[0] < 2563);
}

void test_sci_receive() {
  System s;
  const u16 handler = 0xD000;
  s.vector(kVecSci, handler);
  // Handler: read TRCSR, then RDR (clears RDRF); store the byte at $40+n.
  s.code({0x96, 0x11, 0xD6, 0x12, 0x3C, 0xDE, 0x50, 0xE7, 0x00, 0x08, 0xDF, 0x50, 0x38, 0x3B}, handler);
  // LDAA $11; LDAB $12; PSHX; LDX $50; STAB 0,X; INX; STX $50; PULX; RTI
  s.mem()[0x50] = 0x00;
  s.mem()[0x51] = 0x40;
  s.code({0x86, 0x05, 0x97, 0x10, 0x86, 0x18, 0x97, 0x11, 0x0E, 0x20, 0xFE});  // E/128; RE RIE; CLI
  s.steps(5);
  const u64 t = s.m.now();
  s.p().receive(std::vector<u8>{0x12, 0x34, 0x56}.data(), 3);
  s.run_until(t + 1216 - 4);
  CHECK(!(s.p().sci_status() & 0x80));
  CHECK_EQ(s.cpu().interrupts_taken(), 0u);
  s.run_until(t + 4000);
  CHECK_EQ(s.cpu().interrupts_taken(), 3u);
  CHECK_EQ(s.mem()[0x40], 0x12);
  CHECK_EQ(s.mem()[0x41], 0x34);
  CHECK_EQ(s.mem()[0x42], 0x56);
  CHECK(!(s.p().sci_status() & 0xC0));
}

// Overrun: the second byte finds RDRF set; ORFE, and RDR keeps the first.
void test_sci_overrun() {
  System s;
  s.code({0x20, 0xFE});
  s.reg(kRmcr, 0x04);  // E/16
  s.reg(kTrcsr, 0x08);
  s.p().receive(std::vector<u8>{0xA1, 0xA2}.data(), 2);
  s.run_until(s.m.now() + 400);
  CHECK_EQ(s.p().sci_status() & 0xC0, 0xC0);
  CHECK_EQ(s.reg(kTrcsr) & 0xC0, 0xC0);
  CHECK_EQ(s.reg(kRdr), 0xA1);
  CHECK_EQ(s.p().sci_status() & 0xC0, 0x00);
  // Bytes arriving with the receiver disabled are lost.
  s.reg(kTrcsr, 0x00);
  s.p().receive(0x99);
  CHECK_EQ(s.p().rx_queued(), size_t(0));
}

// Wake-up: frames are ignored until ten ones in a row on the line.
void test_sci_wakeup() {
  System s;
  s.code({0x20, 0xFE});
  s.reg(kRmcr, 0x04);       // E/16
  s.reg(kTrcsr, 0x08);      // RE
  s.p().receive(0x00);      // the line is busy from now
  s.reg(kTrcsr, 0x09);      // WU while the frame is coming in
  s.run_until(s.m.now() + 200);
  CHECK(!(s.p().sci_status() & 0x80));
  CHECK(s.p().sci_status() & 0x01);
  s.run_until(s.m.now() + 200);  // idle for more than ten bit times
  CHECK(!(s.p().sci_status() & 0x01));
  s.p().receive(0x5A);
  s.run_until(s.m.now() + 200);
  CHECK(s.p().sci_status() & 0x80);
}

void test_sci_external_clock() {
  System s;
  std::vector<u64> when;
  s.p().set_tx_hook([&](u8) { when.push_back(s.m.now()); });
  s.code({0x20, 0xFE});
  s.p().set_sci_external_clock(4u << 16);  // 4 E cycles per clock: 32 per bit
  s.reg(kRmcr, 0x0C);
  s.reg(kTrcsr, 0x02);
  s.reg(kTrcsr);
  s.reg(kTdr, 0x33);
  const u64 t = s.m.now();
  s.run_until(t + 1000);
  CHECK_EQ(when.size(), size_t(1));
  if (!when.empty()) CHECK(when[0] - t <= 32 + 10 * 32 + 10 * 32 + 3);
  CHECK_EQ(s.p().port_ddr(2) & 0x04, 0x00);  // P22 is the clock input
}

void test_ports() {
  System s;
  u8 out = 0, dir = 0;
  int calls = 0;
  s.p().set_port_write_hook(1, [&](u8 d, u8 ddr) { out = d; dir = ddr; ++calls; });
  s.p().set_port_input(1, 0xA5);
  CHECK_EQ(s.reg(kP1Data), 0xA5);
  s.reg(kP1Data, 0x3C);
  CHECK_EQ(calls, 1);
  CHECK_EQ(s.reg(kP1Data), 0xA5);  // still inputs
  s.reg(kP1Ddr, 0x0F);
  CHECK_EQ(calls, 2);
  CHECK_EQ(dir, 0x0F);
  CHECK_EQ(out, 0x3C);
  CHECK_EQ(s.reg(kP1Data), 0xAC);  // low nibble driven
  CHECK_EQ(s.p().port_output(1), 0xFC);
  CHECK_EQ(s.reg(kP1Ddr), 0xFF);  // write-only
  s.p().set_port_read_hook(1, [] { return u8(0x00); });
  CHECK_EQ(s.reg(kP1Data), 0x0C);
  // Port 2: five pins, the mode above them; TE claims P24.
  s.p().set_port_input(2, 0x00);
  CHECK_EQ(s.reg(kP2Data), 0x40);
  s.reg(kRmcr, 0x04);
  s.reg(kTrcsr, 0x02);
  CHECK_EQ(s.p().port_ddr(2) & 0x10, 0x10);
  CHECK_EQ(s.reg(kP2Data), 0x50);
  // RAM control: STBY PWR is the program's to set.
  s.reg(kRamCtrl, 0xC0);
  CHECK_EQ(s.reg(kRamCtrl), 0xFF);
  s.p().standby_power_lost();
  CHECK_EQ(s.reg(kRamCtrl), 0x7F);
}

}  // namespace

int main() {
  test_counter_mc6801();
  test_counter_hd6301();
  test_output_compare_interrupt();
  test_overflow_flag();
  test_output_compare_pin();
  test_input_capture();
  test_spurious_sci_vector();
  test_sci_transmit();
  test_sci_transmit_mc6801();
  test_sci_receive();
  test_sci_overrun();
  test_sci_wakeup();
  test_sci_external_clock();
  test_ports();
  return test::finish("m6801_test_periph");
}

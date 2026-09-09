// Serial communication interface tests (H8/510 manual section 13).
#include <vector>

#include "cpu/h8500/machine.hpp"
#include "common/test_util.hpp"

using namespace h8500;

namespace {

constexpr u32 kSmr = 0xFEC8, kBrr = 0xFEC9, kScr = 0xFECA, kTdr = 0xFECB, kSsr = 0xFECC, kRdr = 0xFECD;
constexpr u64 kBit = 320;          // BRR = 9, n = 0: 32 * 10 states = 31250 baud at 10 MHz
constexpr u64 kFrame = 10 * kBit;  // start + 8 data + 1 stop

struct Board {
  Machine m;
  std::vector<u8> sent;
  std::vector<u64> sent_at;
  Board() : m(ChipModel::H8_510, 2) {
    m.bus().map_ram(0x0000, 0xFE80, BusClass::W16_S2);
    m.sci(0).set_tx_sink([this](u8 b, u64 at) { sent.push_back(b); sent_at.push_back(at); });
  }
  Bus& bus() { return m.bus(); }
  Sci& sci() { return m.sci(0); }
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
    bus().write8(0xFF02, 0x05);  // SCI1 priority 5
    bus().write8(kBrr, 9);
  }
  // Read SSR then write the given flags as 0 (the clearing protocol).
  void clear_flags(u8 flags) {
    const u8 s = bus().read8(kSsr);
    bus().write8(kSsr, u8(s & ~flags));
  }
  u16 marker() { return peek16(0xF100); }
  u16 wait_vector(u64 max, u64* at = nullptr) {
    poke16(0xF100, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    if (at) *at = m.now();
    return marker();
  }
};

void test_reset_values_and_reserved_bits() {
  Board b;
  b.start();
  Bus& bus = b.bus();
  CHECK_EQ(bus.read8(kSmr), 0x04);
  CHECK_EQ(bus.read8(kScr), 0x0C);
  CHECK_EQ(bus.read8(kSsr), 0x87);
  CHECK_EQ(bus.read8(kTdr), 0xFF);
  CHECK_EQ(bus.read8(kRdr), 0x00);
  CHECK_EQ(bus.read8(0xFED1), 0xFF);  // SCI2 BRR
  bus.write8(kSmr, 0x00);
  CHECK_EQ(bus.read8(kSmr), 0x04);
  bus.write8(kScr, 0x00);
  CHECK_EQ(bus.read8(kScr), 0x0C);
  bus.write8(kSsr, 0xFF);             // flags cannot be set
  CHECK_EQ(bus.read8(kSsr), 0x87);
  bus.write8(kRdr, 0x55);
  CHECK_EQ(bus.read8(kRdr), 0x00);
  CHECK_EQ(b.sci().bit_states(), kBit);
  CHECK_EQ(b.sci().frame_states(), kFrame);
  bus.write8(kSmr, Sci::kPe | Sci::kStop | 0x01);  // parity, 2 stop, phi/4
  CHECK_EQ(b.sci().frame_states(), 12 * 4 * kBit);
  bus.write8(kSmr, Sci::kCa | 0x02);               // synchronous, phi/16
  CHECK_EQ(b.sci().frame_states(), 8 * 4 * 16 * (9 + 1));
}

// Transmit two bytes back to back: TDRE returns to 1 as soon as the TDR moves
// into the shift register, the sink sees each byte at its frame end, and the
// two frames are exactly one frame time apart.
void test_transmit() {
  Board b;
  b.start();
  Bus& bus = b.bus();
  bus.write8(kScr, Sci::kTe);          // one frame of ones first
  const u64 t0 = b.m.now();
  bus.write8(kTdr, 0x90);
  b.clear_flags(Sci::kTdre);           // TSR busy with the preamble: TDRE stays 0
  CHECK((bus.read8(kSsr) & Sci::kTdre) == 0);
  b.m.run(kFrame + 100);               // preamble ends: byte moves into the TSR
  CHECK((bus.read8(kSsr) & Sci::kTdre) != 0);
  CHECK_EQ(b.sent.size(), 0u);
  bus.write8(kTdr, 0x3C);
  b.clear_flags(Sci::kTdre);
  b.m.run(kFrame);
  CHECK_EQ(b.sent.size(), 1u);
  CHECK_EQ(b.sent[0], 0x90);
  CHECK_EQ(b.sent_at[0] - t0, 2 * kFrame);
  CHECK((bus.read8(kSsr) & Sci::kTdre) != 0);  // second byte loaded at the boundary
  b.m.run(kFrame);
  CHECK_EQ(b.sent.size(), 2u);
  CHECK_EQ(b.sent[1], 0x3C);
  CHECK_EQ(b.sent_at[1] - b.sent_at[0], kFrame);
  // Idle afterwards: nothing more is sent.
  b.m.run(3 * kFrame);
  CHECK_EQ(b.sent.size(), 2u);
  // 7-bit mode masks the top bit.
  bus.write8(kSmr, Sci::kChr);
  bus.write8(kTdr, 0xFF);
  b.clear_flags(Sci::kTdre);
  b.m.run(kFrame);
  CHECK_EQ(b.sent.back(), 0x7F);
}

void test_txi_interrupt() {
  Board b;
  b.start();
  Bus& bus = b.bus();
  bus.write8(kScr, Sci::kTe);
  b.m.run(kFrame + 10);
  // TIE with TDRE already set requests at once (firmware enables TIE only
  // when it has data to send).
  bus.write8(kScr, Sci::kTe | Sci::kTie);
  CHECK_EQ(b.wait_vector(200), 0x6C);  // SCI1 TXI
  bus.write8(kTdr, 0x42);
  b.clear_flags(Sci::kTdre);           // TSR idle: loads at once, TDRE back to 1 -> TXI again
  CHECK(b.m.intc().request(IrqSrc::Sci1Txi));
  bus.write8(kScr, Sci::kTe);          // TIE off: request drops
  CHECK(!b.m.intc().request(IrqSrc::Sci1Txi));
}

// Receive: bytes pushed by the host complete one frame apart; RDRF and RXI;
// a byte completing while RDRF is still set sets ORER and is lost.
void test_receive_and_overrun() {
  Board b;
  b.start();
  Bus& bus = b.bus();
  bus.write8(kScr, Sci::kRe | Sci::kRie);
  const u64 t0 = b.m.now();
  b.sci().receive_byte(0xF8);
  b.sci().receive_byte(0x90);
  b.sci().receive_byte(0x3C);
  CHECK((bus.read8(kSsr) & Sci::kRdrf) == 0);
  u64 at = 0;
  CHECK_EQ(b.wait_vector(2 * kFrame, &at), 0x6A);  // RXI
  CHECK(at >= t0 + kFrame && at <= t0 + kFrame + 40);
  CHECK_EQ(bus.read8(kRdr), 0xF8);
  CHECK((bus.read8(kSsr) & Sci::kRdrf) != 0);
  // Not read in time: the second byte overruns, RDR keeps the first.
  b.m.run(kFrame);
  CHECK((bus.read8(kSsr) & Sci::kOrer) != 0);
  CHECK_EQ(bus.read8(kRdr), 0xF8);
  CHECK(b.m.intc().request(IrqSrc::Sci1Eri));
  // Clear RDRF and ORER; the third byte then lands.
  b.clear_flags(Sci::kRdrf | Sci::kOrer);
  CHECK(!b.m.intc().request(IrqSrc::Sci1Eri));
  b.m.run(kFrame);
  CHECK_EQ(bus.read8(kRdr), 0x3C);
  CHECK((bus.read8(kSsr) & Sci::kRdrf) != 0);
  CHECK_EQ(b.sci().rx_pending(), 0u);
}

void test_receive_errors_and_disabled() {
  Board b;
  b.start();
  Bus& bus = b.bus();
  // RE = 0: the frame completes but is ignored.
  b.sci().receive_byte(0x11);
  b.m.run(kFrame + 50);
  CHECK((bus.read8(kSsr) & Sci::kRdrf) == 0);
  CHECK_EQ(bus.read8(kRdr), 0x00);
  // Framing error: FER set, data transferred, RDRF not set (line break = 0 + FER).
  bus.write8(kScr, Sci::kRe | Sci::kRie);
  b.sci().receive_byte(0x00, true);
  b.m.run(kFrame + 50);
  CHECK((bus.read8(kSsr) & Sci::kFer) != 0);
  CHECK((bus.read8(kSsr) & Sci::kRdrf) == 0);
  CHECK_EQ(bus.read8(kRdr), 0x00);
  CHECK(b.m.intc().request(IrqSrc::Sci1Eri));
  b.clear_flags(Sci::kFer);
  // Parity errors only count when parity is enabled.
  b.sci().receive_byte(0x55, false, true);
  b.m.run(kFrame + 50);
  CHECK((bus.read8(kSsr) & Sci::kPer) == 0);
  CHECK((bus.read8(kSsr) & Sci::kRdrf) != 0);
  CHECK_EQ(bus.read8(kRdr), 0x55);
  b.clear_flags(Sci::kRdrf);
  bus.write8(kSmr, Sci::kPe);
  b.sci().receive_byte(0xAA, false, true);
  b.m.run(11 * kBit + 50);
  CHECK((bus.read8(kSsr) & Sci::kPer) != 0);
  CHECK((bus.read8(kSsr) & Sci::kRdrf) == 0);
  CHECK_EQ(bus.read8(kRdr), 0xAA);
  // 7-bit reception clears the MSB.
  b.clear_flags(Sci::kPer);
  bus.write8(kSmr, Sci::kChr);
  b.sci().receive_byte(0xC5);
  b.m.run(9 * kBit + 50);
  CHECK_EQ(bus.read8(kRdr), 0x45);
}

// A polling loop written in H8 code: wait for RDRF, echo the byte, clear RDRF.
void test_echo_program() {
  Board b;
  b.start();
  b.poke(0x0100, {
      0x15, 0xFE, 0xCC, 0x80,        // L: MOV.B @H'FECC:16,R0     ; SSR
      0xA0, 0xF6,                    //    BTST.B #6,R0            ; RDRF
      0x27, 0xF8,                    //    BEQ L
      0x15, 0xFE, 0xCD, 0x81,        //    MOV.B @H'FECD:16,R1     ; RDR
      0x15, 0xFE, 0xCB, 0x91,        //    MOV.B R1,@H'FECB:16     ; TDR
      0x04, 0x3F, 0x50,              //    AND.B #H'3F,R0          ; clear RDRF and TDRE (both read as 1)
      0x15, 0xFE, 0xCC, 0x90,        //    MOV.B R0,@H'FECC:16     ; SSR
      0x20, 0xE7,                    //    BRA L                   (25 bytes back)
  });
  b.m.cpu().invalidate_all();
  b.m.cpu().regs().pc = 0x0100;
  b.bus().write8(kScr, Sci::kTe | Sci::kRe);
  b.m.run(kFrame + 100);  // preamble
  for (u8 c : {0x90, 0x3C, 0x7F}) b.sci().receive_byte(c);
  b.m.run(8 * kFrame);
  CHECK_EQ(b.sent.size(), 3u);
  if (b.sent.size() == 3) {
    CHECK_EQ(b.sent[0], 0x90);
    CHECK_EQ(b.sent[1], 0x3C);
    CHECK_EQ(b.sent[2], 0x7F);
  }
}

}  // namespace

int main() {
  test_reset_values_and_reserved_bits();
  test_transmit();
  test_txi_interrupt();
  test_receive_and_overrun();
  test_receive_errors_and_disabled();
  test_echo_program();
  return test::finish("test_sci");
}

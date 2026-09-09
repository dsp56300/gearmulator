// SH7014 serial communication interface (manual section 12).
#include <vector>

#include "cpu/sh2/machine.hpp"
#include "cpu/sh2/sci.hpp"
#include "common/test_util.hpp"

using namespace sh2;

namespace {

constexpr u32 kBase = Sci::kBase0;
constexpr u32 kSmr = kBase + 0, kBrr = kBase + 1, kScr = kBase + 2, kTdr = kBase + 3, kSsr = kBase + 4,
              kRdr = kBase + 5;
constexpr u32 kIprf = 0xFFFF8352u;
constexpr u64 kBit = 32;           // BRR = 0, n = 0, asynchronous: 32 states per bit
constexpr u64 kFrame = 10 * kBit;  // start + 8 data + 1 stop

struct System {
  Machine m;
  Sci sci0, sci1;
  std::vector<u8> sent;
  std::vector<bool> sent_mpb;
  std::vector<u64> sent_at;
  static constexpr u32 kRam = 0xFFFFF000u;

  System()
      : m(ChipModel::SH7014, 1),
        sci0(m.sched(), m.cpu(), m.intc(), Sci::kBase0, {IrqSrc::Eri0, IrqSrc::Rxi0, IrqSrc::Txi0, IrqSrc::Tei0}),
        sci1(m.sched(), m.cpu(), m.intc(), Sci::kBase1, {IrqSrc::Eri1, IrqSrc::Rxi1, IrqSrc::Txi1, IrqSrc::Tei1}) {
    sci0.map(m.io());
    sci1.map(m.io());
    m.bus().map_ram(0x00000000, 0x10000, Bus::kClsCs0);
    // Vectors: every interrupt handler writes its full vector number to H'F100 and returns:
    // MOV #v,R0 ; EXTU.B R0,R0 ; MOV.L R0,@R1 ; RTE ; NOP
    for (u32 v = 64; v < 160; ++v) {
      const u32 h = 0x1000 + (v - 64) * 16;
      m.bus().write32(v * 4, h);
      const u16 code[] = {u16(0xE000 | (v & 0xFF)), 0x600C, 0x2102, 0x002B, 0x0009};
      u32 a = h;
      for (u16 w : code) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    }
    // Main program: NOP loop in on-chip RAM.
    const u16 loop[] = {0x0009, 0xAFFD, 0x0009};  // NOP ; BRA -6 ; slot NOP
    u32 a = kRam;
    for (u16 w : loop) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    m.bus().write32(0, kRam);
    m.bus().write32(4, 0xFFFFFBF0u);
    m.cpu().invalidate_all();
    m.reset();
    m.bus().write16(0xFFFF8624, 0x0000);  // WCR1: zero wait states (reset gives 15 to every area)
    sci0.reset();
    sci1.reset();
    m.cpu().regs().sr &= ~Cpu::kIMask;
    m.cpu().regs().r[1] = 0xF100;
    m.bus().write32(0xF100, 0);
    bus().write16(kIprf, 0x0077);  // IPRF: SCI0 (bits 7-4) and SCI1 (bits 3-0) level 7
    bus().write8(kBrr, 0);
    sci0.set_tx_sink([this](u8 b, bool mpb, u64 at) {
      sent.push_back(b);
      sent_mpb.push_back(mpb);
      sent_at.push_back(at);
    });
  }
  Bus& bus() { return m.bus(); }
  Intc& intc() { return m.intc(); }
  u8 ssr() { return bus().read8(kSsr); }
  // Read SSR then write the given flags as 0 (the clearing protocol of 12.2.7).
  void clear_flags(u8 flags) {
    const u8 s = bus().read8(kSsr);
    bus().write8(kSsr, u8(s & ~flags));
  }
  u32 marker() { return bus().read32(0xF100); }
  u32 wait_vector(u64 max, u64* at = nullptr) {
    bus().write32(0xF100, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    if (at) *at = m.now();
    return marker();
  }
};

void test_reset_values_and_reserved_bits() {
  System s;
  Bus& b = s.bus();
  s.sci0.reset();  // the harness programmed BRR0; back to the power-on state
  for (u32 base : {Sci::kBase0, Sci::kBase1}) {
    CHECK_EQ(b.read8(base + 0), 0x00);  // SMR
    CHECK_EQ(b.read8(base + 1), 0xFF);  // BRR
    CHECK_EQ(b.read8(base + 2), 0x00);  // SCR
    CHECK_EQ(b.read8(base + 3), 0xFF);  // TDR
    CHECK_EQ(b.read8(base + 4), 0x84);  // SSR: TDRE, TEND
    CHECK_EQ(b.read8(base + 5), 0x00);  // RDR
  }
  // SMR and SCR have no reserved bits.
  b.write8(kSmr, 0xFF);
  CHECK_EQ(b.read8(kSmr), 0xFF);
  b.write8(kSmr, 0x00);
  b.write8(kScr, 0xFF);
  CHECK_EQ(b.read8(kScr), 0xFF);
  b.write8(kScr, 0x00);
  // SSR: the status flags cannot be written to 1; TEND and MPB are read-only; MPBT is R/W.
  b.write8(kSsr, 0xFF);
  CHECK_EQ(b.read8(kSsr), 0x85);
  b.write8(kSsr, 0x00);
  CHECK_EQ(b.read8(kSsr), 0x84);  // TDRE is locked at 1 while TE = 0; TEND untouched
  b.write8(kSsr, 0x01);
  CHECK((b.read8(kSsr) & Sci::kMpbt) != 0);
  b.write8(kSsr, 0x84);
  CHECK((b.read8(kSsr) & Sci::kMpbt) == 0);
  // RDR is read-only; the two channels are independent.
  b.write8(kRdr, 0x55);
  CHECK_EQ(b.read8(kRdr), 0x00);
  b.write8(Sci::kBase1 + 1, 0x12);
  CHECK_EQ(b.read8(Sci::kBase1 + 1), 0x12);
  CHECK_EQ(b.read8(kBrr), 0xFF);
  // The 16-bit access of table 12.2 composes the two bytes.
  CHECK_EQ(b.read16(kSmr), 0x00FF);  // SMR, BRR
  CHECK_EQ(b.read16(kSsr), 0x8400);  // SSR, RDR
}

// Bit and frame times against the formulas of 12.2.8 (bit time = phi / B):
// asynchronous 64 * 2^(2n-1) * (N+1), clocked synchronous 8 * 2^(2n-1) * (N+1).
void test_bit_rates() {
  System s;
  Bus& b = s.bus();
  // Asynchronous, n = 0, N = 0: 32 states (phi = 4 MHz -> 125000 bit/s, table 12.5).
  CHECK_EQ(s.sci0.bit_states(), 32u);
  CHECK_EQ(s.sci0.frame_states(), 10 * 32u);
  // Asynchronous, n = 0, N = 25: 832 states (phi = 8 MHz -> 9615 bit/s, 0.16% off 9600, table 12.3).
  b.write8(kBrr, 25);
  CHECK_EQ(s.sci0.bit_states(), 32u * 26);
  // Asynchronous, n = 2, N = 12: 64 * 8 * 13 = 6656 states.
  b.write8(kBrr, 12);
  b.write8(kSmr, 0x02);
  CHECK_EQ(s.sci0.bit_states(), 6656u);
  // Frame lengths (table 12.10): 7 data + parity + 2 stop = 11 bits; 8 data + MPB + 1 stop = 11 bits;
  // with MP set, PE is ignored (no extra bit).
  b.write8(kSmr, Sci::kChr | Sci::kPe | Sci::kStop);
  CHECK_EQ(s.sci0.frame_states(), 11 * 32u * (12 + 1));
  b.write8(kSmr, Sci::kMp);
  CHECK_EQ(s.sci0.frame_states(), 11 * 32u * 13);
  b.write8(kSmr, Sci::kMp | Sci::kPe);
  CHECK_EQ(s.sci0.frame_states(), 11 * 32u * 13);
  // Clocked synchronous, n = 0, N = 7: 32 states (phi = 8 MHz -> 250 kbit/s, table 12.4); 8-bit frames.
  b.write8(kSmr, Sci::kCa);
  b.write8(kBrr, 7);
  CHECK_EQ(s.sci0.bit_states(), 32u);
  CHECK_EQ(s.sci0.frame_states(), 8 * 32u);
  // Clocked synchronous, n = 1, N = 99: 8 * 2 * 100 = 1600 states (phi = 4 MHz -> 2.5 kbit/s, table 12.4).
  b.write8(kSmr, Sci::kCa | 0x01);
  b.write8(kBrr, 99);
  CHECK_EQ(s.sci0.bit_states(), 1600u);
  // CHR / PE / STOP / MP do not change the synchronous frame.
  b.write8(kSmr, Sci::kCa | Sci::kChr | Sci::kPe | Sci::kStop | Sci::kMp | 0x01);
  CHECK_EQ(s.sci0.frame_states(), 8 * 1600u);
  // External clock (CKE1 = 1): the host period wins; unset it falls back to the formula.
  b.write8(kScr, Sci::kCke1);
  CHECK_EQ(s.sci0.bit_states(), 1600u);
  s.sci0.set_external_bit_time(100);
  CHECK_EQ(s.sci0.bit_states(), 100u);
  CHECK_EQ(s.sci0.frame_states(), 800u);
  b.write8(kScr, 0);
  CHECK_EQ(s.sci0.bit_states(), 1600u);
  // SCK pin function (table 12.9).
  b.write8(kSmr, 0);
  b.write8(kScr, 0x00);
  CHECK(!s.sci0.sck_output());
  b.write8(kScr, Sci::kCke0);
  CHECK(s.sci0.sck_output());
  b.write8(kScr, Sci::kCke1);
  CHECK(!s.sci0.sck_output() && s.sci0.external_clock());
  b.write8(kSmr, Sci::kCa);
  b.write8(kScr, 0x00);
  CHECK(s.sci0.sck_output());
}

// Transmit two bytes back to back: TDRE returns to 1 as soon as the TDR moves
// into the shift register, TEND drops until the last frame ends, the sink sees
// each byte at its frame end and the two frames are exactly one frame apart.
void test_transmit() {
  System s;
  Bus& b = s.bus();
  b.write8(kScr, Sci::kTe);
  s.m.run(50);                              // marking: nothing is sent by enabling TE
  CHECK_EQ(s.sent.size(), 0u);
  CHECK_EQ(s.ssr() & (Sci::kTdre | Sci::kTend), Sci::kTdre | Sci::kTend);
  b.write8(kTdr, 0x90);
  s.clear_flags(Sci::kTdre);                // TSR idle: loads at once
  const u64 t0 = s.m.now();
  CHECK((s.ssr() & Sci::kTdre) != 0);
  CHECK((s.ssr() & Sci::kTend) == 0);
  b.write8(kTdr, 0x3C);
  s.clear_flags(Sci::kTdre);                // TSR busy: TDRE stays 0 until the frame boundary
  CHECK((s.ssr() & Sci::kTdre) == 0);
  s.m.run(kFrame - 40);
  CHECK_EQ(s.sent.size(), 0u);
  CHECK((s.ssr() & Sci::kTdre) == 0);
  s.m.run(80);
  CHECK_EQ(s.sent.size(), 1u);
  CHECK_EQ(s.sent[0], 0x90);
  CHECK_EQ(s.sent_at[0], t0 + kFrame);
  CHECK((s.ssr() & Sci::kTdre) != 0);       // second byte loaded at the boundary
  CHECK((s.ssr() & Sci::kTend) == 0);
  s.m.run(kFrame);
  CHECK_EQ(s.sent.size(), 2u);
  CHECK_EQ(s.sent[1], 0x3C);
  CHECK_EQ(s.sent_at[1] - s.sent_at[0], kFrame);
  CHECK((s.ssr() & Sci::kTend) != 0);       // TDRE was 1 at the last bit: transmission ended
  CHECK(!s.sent_mpb[0] && !s.sent_mpb[1]);  // no multiprocessor format
  // Idle afterwards: nothing more is sent.
  s.m.run(3 * kFrame);
  CHECK_EQ(s.sent.size(), 2u);
  // 7-bit mode masks the top bit; the frame is one bit shorter.
  b.write8(kSmr, Sci::kChr);
  b.write8(kTdr, 0xFF);
  s.clear_flags(Sci::kTdre);
  const u64 t1 = s.m.now();
  s.m.run(9 * kBit + 20);
  CHECK_EQ(s.sent.size(), 3u);
  CHECK_EQ(s.sent.back(), 0x7F);
  CHECK_EQ(s.sent_at.back(), t1 + 9 * kBit);
  // Clearing TE mid-frame initialises the transmitter (12.5.4): the frame is
  // abandoned, TDRE and TEND read 1, and TDRE cannot be cleared while TE = 0.
  b.write8(kSmr, 0);
  b.write8(kTdr, 0x11);
  s.clear_flags(Sci::kTdre);
  s.m.run(kFrame / 2);
  b.write8(kScr, 0);
  CHECK_EQ(s.ssr() & (Sci::kTdre | Sci::kTend), Sci::kTdre | Sci::kTend);
  s.clear_flags(Sci::kTdre);
  CHECK((s.ssr() & Sci::kTdre) != 0);
  s.m.run(2 * kFrame);
  CHECK_EQ(s.sent.size(), 3u);
}

void test_txi_and_tei_interrupts() {
  System s;
  Bus& b = s.bus();
  // TIE with TDRE already set requests TXI at once (vector 130).
  b.write8(kScr, Sci::kTe | Sci::kTie);
  CHECK_EQ(s.wait_vector(200), 130u);
  b.write8(kTdr, 0x42);
  s.clear_flags(Sci::kTdre);  // TSR idle: loads at once, TDRE back to 1 -> TXI still requested
  CHECK(s.intc().request(IrqSrc::Txi0));
  b.write8(kTdr, 0x43);
  s.clear_flags(Sci::kTdre);  // TSR busy: TDRE = 0, the request drops
  CHECK(!s.intc().request(IrqSrc::Txi0));
  u64 at = 0;
  const u64 t0 = s.m.now();
  CHECK_EQ(s.wait_vector(2 * kFrame, &at), 130u);  // TXI again at the frame boundary
  CHECK(at >= t0 + kFrame - 40 && at <= t0 + kFrame + 40);
  b.write8(kScr, Sci::kTe);   // TIE off: request drops
  CHECK(!s.intc().request(IrqSrc::Txi0));
  // TEI: TEND rises when the last frame ends with TDRE = 1 (vector 131).
  b.write8(kScr, Sci::kTe | Sci::kTeie);
  CHECK(!s.intc().request(IrqSrc::Tei0));  // second frame still in flight
  const u64 end = s.sent_at[0] + kFrame;
  CHECK_EQ(s.wait_vector(2 * kFrame, &at), 131u);
  CHECK(at >= end && at <= end + 40);
  CHECK((s.ssr() & Sci::kTend) != 0);
  CHECK_EQ(s.sent.size(), 2u);
  // TEI is cleared by clearing TDRE (which clears TEND) or by clearing TEIE.
  b.write8(kTdr, 0x44);
  s.clear_flags(Sci::kTdre);
  CHECK(!s.intc().request(IrqSrc::Tei0));
  CHECK((s.ssr() & Sci::kTend) == 0);
  s.m.run(kFrame + 40);
  CHECK(s.intc().request(IrqSrc::Tei0));
  b.write8(kScr, Sci::kTe);
  CHECK(!s.intc().request(IrqSrc::Tei0));
  // TE = 0 sets TEND: with TEIE this requests TEI at once.
  b.write8(kScr, Sci::kTeie);
  CHECK(s.intc().request(IrqSrc::Tei0));
  // Channel 1 uses vectors 132-135: TXI1 = 134 (channel 0 quiet first).
  b.write8(kScr, 0);
  s.m.run(50);
  b.write8(Sci::kBase1 + 2, Sci::kTe | Sci::kTie);
  CHECK_EQ(s.wait_vector(200), 134u);
  b.write8(Sci::kBase1 + 2, 0);
  b.write8(kScr, 0);
}

// Receive: bytes pushed by the host complete one frame apart; RDRF and RXI at
// the frame end; a byte completing while RDRF is still set sets ORER (ERI) and
// is lost.
void test_receive_and_overrun() {
  System s;
  Bus& b = s.bus();
  b.write8(kScr, Sci::kRe | Sci::kRie);
  const u64 t0 = s.m.now();
  s.sci0.receive_byte(0xF8);
  s.sci0.receive_byte(0x90);
  s.sci0.receive_byte(0x3C);
  CHECK_EQ(s.sci0.rx_pending(), 3u);
  CHECK((s.ssr() & Sci::kRdrf) == 0);
  s.m.run(kFrame - 40);
  CHECK((s.ssr() & Sci::kRdrf) == 0);  // still receiving just before the frame end
  u64 at = 0;
  CHECK_EQ(s.wait_vector(2 * kFrame, &at), 129u);  // RXI0
  CHECK(at >= t0 + kFrame && at <= t0 + kFrame + 40);
  CHECK_EQ(b.read8(kRdr), 0xF8);
  CHECK((s.ssr() & Sci::kRdrf) != 0);
  // Not read in time: the second byte overruns, RDR keeps the first, ERI (vector 128).
  s.m.run(kFrame);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kOrer), Sci::kRdrf | Sci::kOrer);
  CHECK_EQ(b.read8(kRdr), 0xF8);
  CHECK(s.intc().request(IrqSrc::Eri0));
  CHECK(s.intc().request(IrqSrc::Rxi0));
  // RIE = 0 drops both requests; let the RXI handler in flight finish, then
  // re-enable with RDRF cleared to see ERI alone.
  b.write8(kScr, Sci::kRe);
  CHECK(!s.intc().request(IrqSrc::Eri0));
  s.m.run(50);
  b.write8(kScr, Sci::kRe | Sci::kRie);
  s.clear_flags(Sci::kRdrf);
  CHECK_EQ(s.wait_vector(100), 128u);
  // Clear ORER; the third byte then lands.
  s.clear_flags(Sci::kOrer);
  CHECK(!s.intc().request(IrqSrc::Eri0));
  s.m.run(kFrame);
  CHECK_EQ(b.read8(kRdr), 0x3C);
  CHECK((s.ssr() & Sci::kRdrf) != 0);
  CHECK_EQ(s.sci0.rx_pending(), 0u);
  // Channel 1: RXI1 = vector 133 (channel 0 quiet first).
  s.clear_flags(Sci::kRdrf);
  b.write8(kScr, 0);
  s.m.run(50);
  b.write8(Sci::kBase1 + 1, 0);
  b.write8(Sci::kBase1 + 2, Sci::kRe | Sci::kRie);
  s.sci1.receive_byte(0x77);
  CHECK_EQ(s.wait_vector(2 * kFrame), 133u);
  CHECK_EQ(b.read8(Sci::kBase1 + 5), 0x77);
}

void test_receive_errors() {
  System s;
  Bus& b = s.bus();
  // RE = 0: the frame completes but is ignored.
  s.sci0.receive_byte(0x11);
  s.m.run(kFrame + 50);
  CHECK((s.ssr() & Sci::kRdrf) == 0);
  CHECK_EQ(b.read8(kRdr), 0x00);
  // Framing error: FER set, data transferred, RDRF not set (table 12.13); ERI.
  b.write8(kScr, Sci::kRe | Sci::kRie);
  s.sci0.receive_byte(0xA5, false, true);
  s.m.run(kFrame + 50);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kErrors), Sci::kFer);
  CHECK_EQ(b.read8(kRdr), 0xA5);
  CHECK(s.intc().request(IrqSrc::Eri0));
  CHECK(!s.intc().request(IrqSrc::Rxi0));
  // Receiving cannot continue while an error flag is set (12.3.2 note): a good
  // byte arriving now is not stored; after clearing FER the next one is.
  s.sci0.receive_byte(0x5A);
  s.m.run(kFrame + 50);
  CHECK((s.ssr() & Sci::kRdrf) == 0);
  CHECK_EQ(b.read8(kRdr), 0xA5);
  s.clear_flags(Sci::kFer);
  CHECK(!s.intc().request(IrqSrc::Eri0));
  s.sci0.receive_byte(0x5B);
  s.m.run(kFrame + 50);
  CHECK((s.ssr() & Sci::kRdrf) != 0);
  CHECK_EQ(b.read8(kRdr), 0x5B);
  s.clear_flags(Sci::kRdrf);
  // Parity errors only count when parity is enabled.
  s.sci0.receive_byte(0x55, false, false, true);
  s.m.run(kFrame + 50);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kErrors), Sci::kRdrf);
  CHECK_EQ(b.read8(kRdr), 0x55);
  s.clear_flags(Sci::kRdrf);
  b.write8(kSmr, Sci::kPe);
  s.sci0.receive_byte(0xAA, false, false, true);
  s.m.run(11 * kBit + 50);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kErrors), Sci::kPer);
  CHECK_EQ(b.read8(kRdr), 0xAA);
  CHECK(s.intc().request(IrqSrc::Eri0));
  s.clear_flags(Sci::kPer);
  // Framing + parity error together: both flags, data transferred.
  s.sci0.receive_byte(0x00, false, true, true);
  s.m.run(11 * kBit + 50);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kErrors), Sci::kFer | Sci::kPer);
  CHECK_EQ(b.read8(kRdr), 0x00);
  s.clear_flags(Sci::kFer | Sci::kPer);
  // Overrun + framing error: ORER and FER, RDR keeps the earlier byte.
  s.sci0.receive_byte(0x33);
  s.sci0.receive_byte(0x44, false, true);
  s.m.run(2 * 11 * kBit + 50);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kErrors), Sci::kRdrf | Sci::kOrer | Sci::kFer);
  CHECK_EQ(b.read8(kRdr), 0x33);
  s.clear_flags(Sci::kRdrf | Sci::kOrer | Sci::kFer);
  // 7-bit reception clears the MSB.
  b.write8(kSmr, Sci::kChr);
  s.sci0.receive_byte(0xC5);
  s.m.run(9 * kBit + 50);
  CHECK_EQ(b.read8(kRdr), 0x45);
  // Clearing RE leaves RDRF / RDR alone (12.2.6).
  b.write8(kScr, 0);
  CHECK((s.ssr() & Sci::kRdrf) != 0);
  CHECK_EQ(b.read8(kRdr), 0x45);
  // DMAC hooks (12.4): a DMAC read of RDR clears RDRF; a DMAC write to TDR
  // clears TDRE and TEND and starts the frame.
  CHECK_EQ(s.sci0.dma_read_rdr(), 0x45);
  CHECK((s.ssr() & Sci::kRdrf) == 0);
  b.write8(kSmr, 0);
  b.write8(kScr, Sci::kTe);
  s.sci0.dma_wrote_tdr(0x66);
  CHECK_EQ(s.ssr() & (Sci::kTdre | Sci::kTend), Sci::kTdre);  // TSR loaded at once, TEND = 0
  s.sci0.dma_wrote_tdr(0x67);
  CHECK_EQ(s.ssr() & (Sci::kTdre | Sci::kTend), 0);
  s.m.run(2 * kFrame + 50);
  CHECK_EQ(s.sent.size(), 2u);
  if (s.sent.size() == 2) {
    CHECK_EQ(s.sent[0], 0x66);
    CHECK_EQ(s.sent[1], 0x67);
    CHECK_EQ(s.sent_at[1] - s.sent_at[0], kFrame);
  }
  CHECK((s.ssr() & Sci::kTend) != 0);
}

// Multiprocessor format (12.3.3): MPBT goes out with each frame; on receive,
// MPB latches the frame's bit and with MPIE set data frames are skipped until
// an ID frame arrives.
void test_multiprocessor() {
  System s;
  Bus& b = s.bus();
  const u64 frame = 11 * kBit;  // start + 8 data + MPB + 1 stop
  b.write8(kSmr, Sci::kMp);
  CHECK_EQ(s.sci0.frame_states(), frame);
  // Transmit: an ID frame (MPBT = 1) followed by a data frame (MPBT = 0).
  b.write8(kScr, Sci::kTe);
  b.write8(kTdr, 0x01);
  b.write8(kSsr, u8(s.ssr() | Sci::kMpbt));  // set MPBT (no flag cleared: all flags written as read)
  s.clear_flags(Sci::kTdre);
  s.m.run(frame + 20);
  b.write8(kTdr, 0xAA);
  b.write8(kSsr, u8(s.ssr() & ~Sci::kMpbt));
  s.clear_flags(Sci::kTdre);
  s.m.run(frame + 20);
  CHECK_EQ(s.sent.size(), 2u);
  if (s.sent.size() == 2) {
    CHECK_EQ(s.sent[0], 0x01);
    CHECK(s.sent_mpb[0]);
    CHECK_EQ(s.sent[1], 0xAA);
    CHECK(!s.sent_mpb[1]);
  }
  // Receive with MPIE: data frames addressed to somebody else are skipped.
  b.write8(kScr, Sci::kRe | Sci::kRie | Sci::kMpie);
  s.sci0.receive_byte(0x02, true);   // ID of another station
  s.m.run(frame + 20);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kMpb), Sci::kRdrf | Sci::kMpb);
  CHECK((b.read8(kScr) & Sci::kMpie) == 0);  // cleared by the ID frame
  CHECK_EQ(b.read8(kRdr), 0x02);
  CHECK(s.intc().request(IrqSrc::Rxi0));
  // Not our ID: set MPIE again and clear RDRF (figure 12.12 step 3).
  b.write8(kScr, Sci::kRe | Sci::kRie | Sci::kMpie);
  s.clear_flags(Sci::kRdrf);
  s.sci0.receive_byte(0x11, false);  // data frames for the other station...
  s.sci0.receive_byte(0x22, false, true);  // ...even with a framing error: no flags
  s.m.run(2 * frame + 20);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kErrors | Sci::kMpb), 0);
  CHECK_EQ(b.read8(kRdr), 0x02);
  CHECK((b.read8(kScr) & Sci::kMpie) != 0);
  CHECK(!s.intc().request(IrqSrc::Rxi0));
  // Our ID arrives: received, MPIE cleared, then data frames come through with MPB = 0.
  s.sci0.receive_byte(0x01, true);
  s.m.run(frame + 20);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kMpb), Sci::kRdrf | Sci::kMpb);
  CHECK_EQ(b.read8(kRdr), 0x01);
  CHECK((b.read8(kScr) & Sci::kMpie) == 0);
  s.clear_flags(Sci::kRdrf);
  s.sci0.receive_byte(0xAA, false);
  s.m.run(frame + 20);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kMpb), Sci::kRdrf);
  CHECK_EQ(b.read8(kRdr), 0xAA);
  s.clear_flags(Sci::kRdrf);
  // Parity is ignored in the multiprocessor format even with PE set.
  b.write8(kSmr, Sci::kMp | Sci::kPe);
  s.sci0.receive_byte(0x55, false, false, true);
  s.m.run(frame + 20);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kErrors), Sci::kRdrf);
  s.clear_flags(Sci::kRdrf);
  // MPIE without MP is ignored: normal reception.
  b.write8(kSmr, 0);
  b.write8(kScr, Sci::kRe | Sci::kRie | Sci::kMpie);
  s.sci0.receive_byte(0x99, false);
  s.m.run(kFrame + 20);
  CHECK((s.ssr() & Sci::kRdrf) != 0);
  CHECK_EQ(b.read8(kRdr), 0x99);
  CHECK((b.read8(kScr) & Sci::kMpie) != 0);
}

// Clocked synchronous mode (12.3.4): 8-bit frames at the serial clock, no
// framing / parity errors, ORER only; a set error flag also blocks
// transmission (12.5.5); external clock through set_external_bit_time.
void test_clocked_synchronous() {
  System s;
  Bus& b = s.bus();
  b.write8(kSmr, Sci::kCa | Sci::kChr | Sci::kPe);  // CHR / PE ignored
  b.write8(kBrr, 1);                                // 4 * 2 = 8 states per bit
  const u64 frame = 8 * 8;
  CHECK_EQ(s.sci0.frame_states(), frame);
  b.write8(kScr, Sci::kTe | Sci::kRe | Sci::kRie);
  // Transmit two bytes back to back.
  b.write8(kTdr, 0xC3);
  s.clear_flags(Sci::kTdre);
  const u64 t0 = s.m.now();
  b.write8(kTdr, 0x81);
  s.clear_flags(Sci::kTdre);
  s.m.run(2 * frame + 20);
  CHECK_EQ(s.sent.size(), 2u);
  if (s.sent.size() == 2) {
    CHECK_EQ(s.sent[0], 0xC3);  // full 8 bits despite CHR
    CHECK_EQ(s.sent[1], 0x81);
    CHECK_EQ(s.sent_at[0], t0 + frame);
    CHECK_EQ(s.sent_at[1], t0 + 2 * frame);
  }
  CHECK((s.ssr() & Sci::kTend) != 0);
  // Receive: framing / parity attributes are meaningless here.
  s.sci0.receive_byte(0x3C, false, true, true);
  s.m.run(frame - 20);
  CHECK((s.ssr() & Sci::kRdrf) == 0);
  s.m.run(40);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kErrors), Sci::kRdrf);
  CHECK_EQ(b.read8(kRdr), 0x3C);
  // Overrun: ORER; while it is set TDRE = 0 does not start a frame (12.5.5).
  s.sci0.receive_byte(0x7E);
  s.m.run(frame + 20);
  CHECK_EQ(s.ssr() & (Sci::kRdrf | Sci::kErrors), Sci::kRdrf | Sci::kOrer);
  b.write8(kTdr, 0x5A);
  s.clear_flags(Sci::kTdre);
  CHECK((s.ssr() & Sci::kTdre) == 0);
  s.m.run(2 * frame);
  CHECK_EQ(s.sent.size(), 2u);
  CHECK((s.ssr() & Sci::kTdre) == 0);
  s.clear_flags(Sci::kRdrf | Sci::kOrer);  // released: the frame starts now
  const u64 t2 = s.m.now();
  CHECK((s.ssr() & Sci::kTdre) != 0);
  s.m.run(frame + 20);
  CHECK_EQ(s.sent.size(), 3u);
  CHECK_EQ(s.sent.back(), 0x5A);
  CHECK_EQ(s.sent_at.back(), t2 + frame);
  // External clock: CKE1 = 1 with a host-supplied period of 25 states per bit.
  b.write8(kScr, Sci::kTe | Sci::kCke1);
  s.sci0.set_external_bit_time(25);
  CHECK_EQ(s.sci0.frame_states(), 200u);
  b.write8(kTdr, 0x0F);
  s.clear_flags(Sci::kTdre);
  const u64 t3 = s.m.now();
  s.m.run(200 + 20);
  CHECK_EQ(s.sent.size(), 4u);
  CHECK_EQ(s.sent_at.back(), t3 + 200);
  CHECK(!s.sci0.sck_output());
}

// A polling loop written in SH-2 code: wait for RDRF, read RDR, clear RDRF,
// wait for TDRE, write TDR, clear TDRE; every byte received comes back out.
void test_echo_program() {
  System s;
  const u16 code[] = {
      0xD209,  //        MOV.L  @(9,PC),R2    ; R2 = H'FFFF81A0
      0x8424,  // rx:    MOV.B  @(4,R2),R0    ; SSR
      0xC840,  //        TST    #H'40,R0      ; RDRF?
      0x89FC,  //        BT     rx
      0x8425,  //        MOV.B  @(5,R2),R0    ; RDR
      0x6303,  //        MOV    R0,R3
      0x8424,  //        MOV.B  @(4,R2),R0    ; SSR (RDRF read as 1)
      0xC9BF,  //        AND    #H'BF,R0      ; RDRF := 0
      0x8024,  //        MOV.B  R0,@(4,R2)
      0x8424,  // tx:    MOV.B  @(4,R2),R0    ; SSR
      0xC880,  //        TST    #H'80,R0      ; TDRE?
      0x89FC,  //        BT     tx
      0x6033,  //        MOV    R3,R0
      0x8023,  //        MOV.B  R0,@(3,R2)    ; TDR
      0x8424,  //        MOV.B  @(4,R2),R0    ; SSR (TDRE read as 1)
      0xC97F,  //        AND    #H'7F,R0      ; TDRE := 0
      0x8024,  //        MOV.B  R0,@(4,R2)
      0xAFEE,  //        BRA    rx
      0x0009,  //        NOP
      0x0009,  //        (pad)
      0xFFFF, 0x81A0,  // .long H'FFFF81A0
  };
  u32 a = 0x2000;
  for (u16 w : code) { Bus::put_be16(s.bus().ptr(a), w); a += 2; }
  s.bus().write32(0, 0x2000);
  s.m.cpu().invalidate_all();
  s.m.reset();
  s.m.bus().write16(0xFFFF8624, 0x0000);
  s.sci0.reset();
  s.bus().write8(kBrr, 0);
  s.bus().write8(kScr, Sci::kTe | Sci::kRe);
  s.m.run(100);
  for (u8 c : {0x90, 0x3C, 0x7F, 0x00}) s.sci0.receive_byte(c);
  s.m.run(8 * kFrame);
  CHECK_EQ(s.sent.size(), 4u);
  if (s.sent.size() == 4) {
    CHECK_EQ(s.sent[0], 0x90);
    CHECK_EQ(s.sent[1], 0x3C);
    CHECK_EQ(s.sent[2], 0x7F);
    CHECK_EQ(s.sent[3], 0x00);
    // Echoed frames follow the received ones, back to back or nearly so.
    for (size_t i = 1; i < 4; ++i) {
      CHECK(s.sent_at[i] - s.sent_at[i - 1] >= kFrame);
      CHECK(s.sent_at[i] - s.sent_at[i - 1] <= kFrame + 100);
    }
  }
  CHECK_EQ(s.bus().read8(kSsr) & (Sci::kRdrf | Sci::kErrors), 0);
}

}  // namespace

int main() {
  test_reset_values_and_reserved_bits();
  test_bit_rates();
  test_transmit();
  test_txi_and_tei_interrupts();
  test_receive_and_overrun();
  test_receive_errors();
  test_multiprocessor();
  test_clocked_synchronous();
  test_echo_program();
  return test::finish("test_sh2_sci");
}

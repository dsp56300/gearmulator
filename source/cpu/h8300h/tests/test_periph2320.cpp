// H8S/2320 peripheral tests: the interrupt controller's mode 2 priorities,
// the TPU's compare matches and counter clearing, the DMAC's sequential and
// repeat modes driven by the TPU and the SCI, the SCI's character timing,
// reception and clocked synchronous exchange, the A/D converter's scan and
// single modes, and the ports.
#include <vector>

#include "cpu/h8300h/machine2320.hpp"
#include "common/test_util.hpp"

using namespace h8300h;

namespace {

constexpr u32 kCode = 0x001000;
constexpr u32 kIpra = 0xFFFEC4, kAstcr = 0xFFFED1, kSyscr = 0xFFFF39;
constexpr u32 kTstr = 0xFFFFC0, kTcr0 = 0xFFFFD0, kTier0 = 0xFFFFD4, kTsr0 = 0xFFFFD5, kTcnt0 = 0xFFFFD6, kTgr0a = 0xFFFFD8;
constexpr u32 kTcr1 = 0xFFFFE0, kTgr1a = 0xFFFFE8;
constexpr u32 kSci0 = 0xFFFF78, kSci1 = 0xFFFF80;  // SMR BRR SCR TDR SSR RDR SCMR
constexpr u32 kAdcsr = 0xFFFF98, kAdcr = 0xFFFF99;
constexpr u32 kDmabcrh = 0xFFFF06, kDmabcrl = 0xFFFF07;

// The core asleep with every interrupt masked, so time advances exactly as asked.
struct System {
  Machine2320 m;
  System() {
    m.map_ram(0, 0x10000);
    u8* mem = m.bus().mem();
    mem[0] = 0; mem[1] = u8(kCode >> 16); mem[2] = u8(kCode >> 8); mem[3] = u8(kCode);
    mem[kCode] = 0x01; mem[kCode + 1] = 0x80;  // SLEEP
    mem[kCode + 2] = 0x40; mem[kCode + 3] = 0xFC;  // BRA to it
    m.reset();
    w8(kAstcr, 0x00);
    m.run(16);
    CHECK(m.cpu().sleeping());
  }
  Peripherals2320& p() { return m.periph(); }
  u8 r8(u32 a) { return m.bus().read8(a); }
  u16 r16(u32 a) { const u8 hi = r8(a); return u16((hi << 8) | r8(a + 1)); }  // upper byte first, as the core does
  void w8(u32 a, u8 v) { m.bus().write8(a, v); }
  void w16(u32 a, u16 v) { w8(a, u8(v >> 8)); w8(a + 1, u8(v)); }
  void wait(u64 states) { m.run(states); }
};

void test_mode2_priorities() {
  System s;
  s.w8(kSyscr, 0x21);  // INTM = 2
  CHECK_EQ(s.p().control_mode(), 2u);
  s.w8(kIpra + 10 / 2, 0x50);  // IPRF upper half (field 10): TPU0 at 5
  s.w8(kIpra + 19 / 2, 0x03);  // IPRJ lower half (field 19): SCI0 at 3
  s.p().request(32);           // TGI0A
  s.p().request(81);           // RXI0
  unsigned which = 0;
  CHECK(s.p().arbitrate(which, 0x78 | 2));
  CHECK_EQ(which, 32u);         // the higher level wins over the lower vector
  CHECK_EQ(unsigned(s.p().level(32)), 5u);
  CHECK(!s.p().arbitrate(which, 0x78 | 5));  // nothing above mask 5
  s.p().acknowledge(32);
  CHECK(s.p().arbitrate(which, 0x78 | 2));
  CHECK_EQ(which, 81u);
  s.w8(kIpra + 10 / 2, 0x30);  // a tie: the lower vector goes first
  s.p().request(32);
  CHECK(s.p().arbitrate(which, 0x78));
  CHECK_EQ(which, 32u);
  s.p().request(7);  // NMI before everything, whatever the mask
  CHECK(s.p().arbitrate(which, 0x7F));
  CHECK_EQ(which, 7u);
  // mode 0: the I bit masks
  s.p().acknowledge(7);
  s.w8(kSyscr, 0x01);
  CHECK(!s.p().arbitrate(which, kCcrI));
  CHECK(s.p().arbitrate(which, 0));
  CHECK_EQ(which, 32u);
}

void test_tpu_compare() {
  System s;
  s.w8(kTcr0, 0x20);       // phi/1, cleared by TGRA
  s.w16(kTgr0a, 4);         // a match every five clocks
  s.w8(kTier0, 0x41);       // TGIEA
  s.w8(kTstr, 0x01);
  s.wait(4);
  CHECK_EQ(s.r16(kTcnt0), 4);
  CHECK(!s.p().requested(32));
  s.wait(1);                // the clock at which TCNT holds TGRA: the flag, and TCNT starts over
  CHECK_EQ(s.r16(kTcnt0), 0);
  CHECK_EQ(s.r8(kTsr0) & 1, 1);
  CHECK(s.p().requested(32));
  s.w8(kTsr0, 0xFE);        // a 0 clears the flag and withdraws the request
  CHECK(!s.p().requested(32));
  s.wait(5 * 3);
  CHECK_EQ(s.r8(kTsr0) & 1, 1);
  CHECK_EQ(s.r16(kTcnt0), 0);
  // phi/4, both edges: a count every two states
  s.w8(kTstr, 0x00);
  s.w8(kTsr0, 0xC0);
  s.w16(kTcnt0, 0);
  s.w8(kTcr0, 0x31);        // phi/4, both edges, cleared by TGRA
  s.w16(kTgr0a, 0xFFFF);
  s.w8(kTstr, 0x01);
  const u64 t0 = s.m.now();
  s.wait(2 * 100 - (t0 & 1));
  CHECK_EQ(s.r16(kTcnt0), 100);
  // the overflow, on channel 1, which has two general registers: TCIV1 is vector 42
  s.w8(kTcr1, 0x00);
  s.w16(kTgr1a, 0x8000);
  s.w8(kTcr1 + 4, 0x50);    // TCIEV
  s.w16(kTcr1 + 6, 0xFFFE);
  s.w8(kTstr, 0x02);
  s.wait(1);
  CHECK(!s.p().requested(42));
  s.wait(1);
  CHECK(s.p().requested(42));
  CHECK_EQ(s.r8(kTcr1 + 5) & 0x10, 0x10);
}

void test_dmac() {
  System s;
  constexpr u32 source = 0x002000;
  for (u8 i = 0; i < 8; ++i) s.m.bus().mem()[source + i] = u8(0x11 * (i + 1));

  // channel 0A: sequential, byte, MAR increments and is the source, four transfers to H'FFF000 in the on-chip RAM,
  // the end interrupt on, activated by TPU0's TGRA compare match, which it acknowledges (DTA)
  s.w8(0xFFFEE1, u8(source >> 16)); s.w8(0xFFFEE2, u8(source >> 8)); s.w8(0xFFFEE3, u8(source));
  s.w16(0xFFFEE4, 0xF000);
  s.w16(0xFFFEE6, 4);
  s.w8(0xFFFF02, 8);       // DMACR0A: TPU0 compare match A
  s.w8(kDmabcrh, 0x01);    // DTA0A
  s.w8(kDmabcrl, 0x11);    // DTE0A, DTIE0A
  s.w8(kTcr0, 0x20);       // phi/1, cleared by TGRA
  s.w16(kTgr0a, 1);        // a match every two states
  s.w8(kTier0, 0x41);
  s.w8(kTstr, 0x01);
  s.wait(2);
  CHECK_EQ(s.r8(0xFFF000), 0x11);
  CHECK_EQ(s.r8(kTsr0) & 1, 0);     // taken with DTA: the flag never shows
  CHECK(!s.p().requested(32));
  s.wait(6);
  CHECK_EQ(s.r8(0xFFF000), 0x44);   // IOAR does not move, so the last byte is there
  CHECK_EQ(s.r8(0xFFFEE3), 0x04);   // MAR advanced by four
  CHECK_EQ(s.r8(kDmabcrl), 0x01);   // DTE0A cleared, DTIE0A still set
  CHECK(s.p().requested(72));        // DEND0A
  s.p().acknowledge(72);
  s.wait(2);
  CHECK(!s.p().requested(72));       // only once, not for as long as it holds
  CHECK_EQ(s.r8(kTsr0) & 1, 1);     // and the TPU's own request is back
  CHECK(s.p().requested(32));
  s.w8(kTstr, 0x00);

  // channel 0B: repeat, two transfers per round, activated by TPU1: after two of them MAR is back where it started
  s.w8(0xFFFEE9, u8(source >> 16)); s.w8(0xFFFEEA, u8(source >> 8)); s.w8(0xFFFEEB, u8(source));
  s.w16(0xFFFEEC, 0xF001);
  s.w16(0xFFFEEE, 0x0202);  // ETCRH and ETCRL both two
  s.w8(0xFFFF03, 0x20 | 9); // DMACR0B: RPE, TPU1 compare match A
  s.w8(kDmabcrl, 0x20);     // DTE0B, every DTIE off
  s.w8(kTcr1, 0x20);
  s.w16(kTgr1a, 1);
  s.w8(kTstr, 0x02);
  s.wait(2);
  CHECK_EQ(s.r8(0xFFFEEB), 0x01);
  s.wait(2);
  CHECK_EQ(s.r8(0xFFFEEB), 0x00);   // MAR restored
  CHECK_EQ(s.r8(0xFFFEEF), 0x02);   // the counter reloaded from ETCRH
  CHECK_EQ(s.r8(kDmabcrl), 0x20);   // still enabled, repeat mode never ends
  CHECK_EQ(s.r8(0xFFF001), 0x22);
}

void test_sci_async() {
  System s;
  std::vector<u8> sent;
  std::vector<u64> when;
  s.p().set_tx_hook(1, [&](u8 b) { sent.push_back(b); when.push_back(s.m.now()); });
  s.w8(kSci1 + 0, 0x00);   // 8N1, phi
  s.w8(kSci1 + 1, 9);      // 31250 bit/s at 10 MHz: 320 states a bit
  CHECK_EQ(s.r8(kSci1 + 4) & 0x84, 0x84);  // TDRE and TEND held while TE is clear
  s.w8(kSci1 + 2, 0xB0);   // TIE, TE, RE
  CHECK(s.p().requested(82 + 4));  // TXI1
  const u64 t0 = s.m.now();
  s.w8(kSci1 + 3, 0x90);
  CHECK_EQ(s.r8(kSci1 + 4) & 0x80, 0x80);  // the CPU's TDR write leaves TDRE alone
  s.w8(kSci1 + 4, 0x7F);   // clearing TDRE hands the byte over
  CHECK_EQ(s.r8(kSci1 + 4) & 0x84, 0x80);  // in the shift register: TDR free again, TEND down
  s.w8(kSci1 + 3, 0x3C);
  s.w8(kSci1 + 4, 0x7F);
  CHECK(!s.p().requested(82 + 4));
  s.wait(3200 * 2 + 10);
  CHECK_EQ(sent.size(), size_t(2));
  if (sent.size() == 2) {
    CHECK_EQ(sent[0], 0x90);
    CHECK_EQ(sent[1], 0x3C);
    CHECK_EQ(when[0], t0 + 3200);
    CHECK_EQ(when[1], t0 + 6400);  // back to back
  }
  CHECK_EQ(s.r8(kSci1 + 4) & 0x84, 0x84);

  // reception: the first byte at once, the next a character later; RXI while RDRF, ERI on an overrun
  s.w8(kSci1 + 2, 0x70);   // RIE, TE, RE
  s.p().receive(1, 0x41);
  s.p().receive(1, 0x42);
  s.p().receive(1, 0x43);
  s.wait(1);
  CHECK_EQ(s.r8(kSci1 + 4) & 0x40, 0x40);
  CHECK(s.p().requested(81 + 4));
  CHECK_EQ(s.r8(kSci1 + 5), 0x41);
  CHECK(!s.p().requested(81 + 4));
  s.wait(3200);
  CHECK_EQ(s.r8(kSci1 + 5), 0x42);
  s.wait(3200);            // the third arrives, RDRF is set, and nobody reads it
  s.p().receive(1, 0x44);
  s.wait(3200);
  CHECK_EQ(s.r8(kSci1 + 4) & 0x20, 0x20);  // ORER
  CHECK(s.p().requested(80 + 4));
  CHECK_EQ(s.r8(kSci1 + 5), 0x43);
}

void test_sci_sync_dma() {
  // SCI0 clocked synchronous with a peer that answers with the byte plus one; a DMA channel feeds TDR on TXI0
  System s;
  std::vector<u8> sent;
  s.p().set_peer(0, [&](u8 b) { sent.push_back(b); return u8(b + 1); });
  for (u8 i = 0; i < 3; ++i) s.m.bus().mem()[0x3000 + i] = u8(0xA0 + i);
  s.w8(kSci0 + 0, 0x80);   // synchronous, phi
  s.w8(kSci0 + 1, 3);      // 4 * 4 states a bit, 128 a byte
  s.w8(0xFFFEE1, 0x00); s.w8(0xFFFEE2, 0x30); s.w8(0xFFFEE3, 0x00);
  s.w16(0xFFFEE4, u16(kSci0 + 3));  // IOAR: TDR0
  s.w16(0xFFFEE6, 3);
  s.w8(0xFFFF02, 4);       // DMACR0A: SCI0 TXI, byte, memory to I/O
  s.w8(kDmabcrh, 0x01);    // DTA0A
  s.w8(kDmabcrl, 0x10);    // DTE0A
  s.w8(kSci0 + 2, 0x30);   // TE, RE: TDRE is set, and the DMAC takes the request at once
  s.wait(128 * 4);
  CHECK_EQ(sent.size(), size_t(3));
  if (sent.size() == 3) {
    CHECK_EQ(sent[0], 0xA0);
    CHECK_EQ(sent[2], 0xA2);
  }
  CHECK_EQ(s.r8(kDmabcrl), 0x00);  // three transfers: done
  CHECK_EQ(s.r8(kSci0 + 4) & 0x60, 0x60);  // RDRF with an overrun: nobody read the answers
  CHECK_EQ(s.r8(kSci0 + 5), 0xA1);  // the first answer stayed
}

// The A/D the way firmware here runs it - scan mode over AN0 to AN3 with CKS1 and CKS set - and then single
// mode with the interrupt: when each result lands, what TEMP hands out, what ADF and ADST do (16.3, 16.4).
void test_adc() {
  System s;
  for (unsigned c = 0; c < 8; ++c) s.p().set_analog(c, u16(100 * (c + 1) + 1));
  CHECK_EQ(s.r8(kAdcr), 0x3F);  // the reserved bits read 1
  s.w8(kAdcr, 0x7F);            // CKS1
  s.w8(kAdcsr, 0x3B);           // ADST, SCAN, CKS, AN0-AN3
  s.wait(133);                  // the first conversion takes 134 states, every one after it 128
  CHECK_EQ(s.r16(0xFFFF90), 0);
  s.wait(1);
  CHECK_EQ(s.r16(0xFFFF90), 101 << 6);
  s.p().set_analog(0, 1023);    // taken when AN0 and AN1 come round again
  s.p().set_analog(1, 0);
  s.wait(3 * 128 - 1);
  CHECK_EQ(s.r16(0xFFFF96), 0);
  CHECK(!(s.r8(kAdcsr) & 0x80));  // no ADF before the last channel is in
  s.wait(1);
  CHECK_EQ(s.r16(0xFFFF96), 401 << 6);
  CHECK_EQ(s.r8(kAdcsr), 0xBB);   // ADF, and it keeps going
  // reading the upper byte parks the lower one in TEMP, which the next lower byte read returns
  CHECK_EQ(s.r8(0xFFFF90), (101 << 6) >> 8);
  s.wait(128);
  CHECK_EQ(s.r8(0xFFFF91), (101 << 6) & 0xFF);
  CHECK_EQ(s.r16(0xFFFF90), 0xFFC0);
  // AN1's second conversion has just started: clearing ADST loses it, and the 0 in bit 7 clears ADF
  s.w8(kAdcsr, 0x1B);
  s.wait(1000);
  CHECK_EQ(s.r16(0xFFFF92), 201 << 6);
  CHECK_EQ(s.r8(kAdcsr), 0x1B);
  // single mode, AN5, interrupt on, CKS clear: 266 states, then ADF, ADST cleared and ADI
  s.w8(kAdcsr, 0x65);
  s.wait(265);
  CHECK_EQ(s.r8(kAdcsr), 0x65);
  CHECK(!s.p().requested(28));
  s.wait(1);
  CHECK_EQ(s.r16(0xFFFF92), 601 << 6);  // AN5 lands in ADDRB
  CHECK_EQ(s.r8(kAdcsr), 0xC5);
  CHECK(s.p().requested(28));
  // writing 0 to ADF clears it and withdraws the request, and software cannot set it
  s.w8(kAdcsr, 0x45);
  CHECK(!s.p().requested(28));
  s.w8(kAdcsr, 0xC5);
  CHECK_EQ(s.r8(kAdcsr), 0x45);
}

void test_ports() {
  System s;
  std::vector<std::pair<unsigned, u8>> writes;
  s.p().set_port_input([](Peripherals2320::Port p) { return u8(p == Peripherals2320::P6 ? 0x5A : 0xFF); });
  s.p().set_port_write_hook([&](Peripherals2320::Port p, u8 v) { writes.push_back({unsigned(p), v}); });
  s.w8(0xFFFEB5, 0x0F);  // P6DDR: the low half drives
  s.w8(0xFFFF65, 0x33);  // P6DR
  CHECK_EQ(s.r8(0xFFFF55), 0x53);  // PORT6: the pins on the inputs, the data register on the outputs
  CHECK_EQ(unsigned(s.p().port_output(Peripherals2320::P6)), 0x03u);
  CHECK_EQ(writes.size(), size_t(1));  // the data register write; the board looks at the outputs then
  s.w8(0xFFFF55, 0x00);  // PORTn is read only
  CHECK_EQ(s.r8(0xFFFF65), 0x33);
}

// TPU channel 2 as the firmware programs it - phi/4, cleared by a TGRA match at 1249, TGIA on, priority 2 - taken
// by a running CPU in interrupt control mode 2: one period of 1250 counts of four states, the frame with EXR, the
// mask raised to the priority, and RTE back.
void test_tpu_interrupt_taken() {
  System s;
  constexpr u32 handler = 0x001100, program = 0x001200, sp = 0x00FC00;
  u8* mem = s.m.bus().mem();
  mem[44 * 4] = 0; mem[44 * 4 + 1] = 0; mem[44 * 4 + 2] = u8(handler >> 8); mem[44 * 4 + 3] = u8(handler);
  mem[handler] = 0x00; mem[handler + 1] = 0x00;  // NOP: the entry and the handler's first instruction are one step
  mem[handler + 2] = 0x56; mem[handler + 3] = 0x70;  // RTE
  mem[program] = 0x40; mem[program + 1] = 0xFE;  // BRA to itself
  s.m.cpu().invalidate_range(0, 0x2000);
  s.w8(kSyscr, 0x21);
  s.w8(0xFFFFF0, 0x29);   // TCR2
  s.w8(0xFFFFF4, 0x41);   // TIER2
  s.w16(0xFFFFF8, 1249);  // TGR2A
  s.w8(0xFFFECA, 0x20);   // IPRG: TPU2 at 2
  s.m.cpu().set_exr(0);
  s.m.cpu().set_ccr(0);
  s.m.cpu().set_sp(sp);
  s.m.cpu().set_pc(program);
  s.w8(kTstr, 0x04);
  const u64 t0 = s.m.now();
  u64 taken = 0;
  for (unsigned i = 0; i < 10000 && !taken; ++i) {
    s.m.step();
    if (s.m.cpu().pc() == handler + 2) taken = s.m.now() - t0;
  }
  CHECK(taken >= 5000 && taken < 5040);       // one period, give or take an instruction
  CHECK_EQ(s.m.cpu().sp(), sp - 6);           // EXR, a reserved byte, CCR and PC
  CHECK_EQ(s.m.cpu().exr() & 7, 2);           // masked at the priority it was accepted with
  CHECK_EQ(s.r8(0xFFFFF5) & 1, 1);            // TGFA, which the handler here leaves alone
  s.m.step();                                  // the RTE, which holds interrupts off for one instruction
  CHECK_EQ(s.m.cpu().sp(), sp);
  CHECK_EQ(s.m.cpu().pc(), program);
  s.m.step();                                  // the BRA
  s.m.step();                                  // and with the flag still set, the interrupt again
  CHECK_EQ(s.m.cpu().pc(), handler + 2);
  CHECK_EQ(s.m.cpu().sp(), sp - 6);
  s.w8(0xFFFFF5, 0xFE);                        // cleared now
  s.m.step();                                  // the RTE
  CHECK_EQ(s.m.cpu().sp(), sp);
  CHECK_EQ(s.m.cpu().pc(), program);
  CHECK_EQ(s.m.cpu().exr() & 7, 0);
}

}  // namespace

int main() {
  test_mode2_priorities();
  test_tpu_compare();
  test_dmac();
  test_sci_async();
  test_sci_sync_dma();
  test_adc();
  test_ports();
  test_tpu_interrupt_taken();
  return test::finish("h8300h_test_periph2320");
}

// Peripheral tests: the TMU underflow interrupt with INTEVT / INTEVT2, the
// IRQ pins in edge and level modes, the PINT pins, the encoded IRL mode and SR.IMASK, the
// SCI and SCIF receive paths, the watchdog, the refresh counter, the DMAC
// in auto-request mode, the I/O ports and the A/D converter.
#include <initializer_list>

#include "common/test_util.hpp"
#include "cpu/sh3/machine.hpp"

using namespace sh3;

namespace {

constexpr u32 kRam = 0x0C000000u, kCode = 0x8C001000u, kStack = 0x8C010000u;
// P4 registers
constexpr u32 kTstr = 0xFFFFFE92u, kTcor0 = 0xFFFFFE94u, kTcnt0 = 0xFFFFFE98u, kTcr0 = 0xFFFFFE9Cu;
constexpr u32 kIcr0 = 0xFFFFFEE0u, kIpra = 0xFFFFFEE2u, kIprb = 0xFFFFFEE4u;
constexpr u32 kScscr = 0xFFFFFE84u, kScssr = 0xFFFFFE88u, kScrdr = 0xFFFFFE8Au;
constexpr u32 kRtcsr = 0xFFFFFF6Eu, kRtcor = 0xFFFFFF72u, kRfcr = 0xFFFFFF74u;
constexpr u32 kWtcsr = 0xFFFFFF86u;
// area-1 registers through P2
constexpr u32 kIntevt2 = 0xA4000000u, kIrr0 = 0xA4000004u, kIcr1 = 0xA4000010u;
constexpr u32 kIcr2 = 0xA4000012u, kPinter = 0xA4000014u;
constexpr u32 kIprc = 0xA4000016u, kIprd = 0xA4000018u, kIpre = 0xA400001Au;
constexpr u32 kSar0 = 0xA4000020u, kDar0 = 0xA4000024u, kDmatcr0 = 0xA4000028u, kChcr0 = 0xA400002Cu, kDmaor = 0xA4000060u;
constexpr u32 kAddrch = 0xA4000088u, kAddrcl = 0xA400008Au, kAdcsr = 0xA4000090u;
constexpr u32 kPacr = 0xA4000100u, kPbcr = 0xA4000102u, kPadr = 0xA4000120u, kPbdr = 0xA4000122u;
constexpr u32 kScscr2 = 0xA4000154u, kScssr2 = 0xA4000158u, kScfrdr2 = 0xA400015Au, kScfdr2 = 0xA400015Eu;

struct System {
  Machine m;
  System() {
    m.map_rom(0, 0x10000);
    m.map_ram(kRam, 0x40000);
    m.reset();
    m.cpu().regs().pc = kCode;
    m.cpu().regs().r[15] = kStack;
    m.cpu().regs().vbr = 0x8C020000u;
    // Interrupt handler at VBR+0x600: count in R7 of the exception bank, RTE.
    code({0x7701, 0x002B, 0x0009}, m.cpu().regs().vbr + 0x600);  // ADD #1,R7; RTE; NOP
    m.cpu().set_sr(kSrMD);  // bank 0, BL clear, IMASK 0
    spin();
  }
  void code(std::initializer_list<u16> words, u32 at) {
    u32 a = at;
    for (u16 w : words) { m.bus().write16(a, w); a += 2; }
    m.cpu().invalidate_range(at, u32(words.size() * 2));
  }
  void spin() { code({0xAFFE, 0x0009}, kCode); }  // BRA self; NOP
  u32 reg32(u32 a) { return m.bus().read32(a); }
  u16 reg16(u32 a) { return m.bus().read16(a); }
  u8 reg8(u32 a) { return m.bus().read8(a); }
  void reg32(u32 a, u32 v) { m.bus().write32(a, v); }
  void reg16(u32 a, u16 v) { m.bus().write16(a, v); }
  void reg8(u32 a, u8 v) { m.bus().write8(a, v); }
  // R7 of bank 1: the active bank inside the handler, the inactive one outside.
  u32 handled() { return (m.cpu().regs().sr & kSrRB) ? m.cpu().regs().r[7] : m.cpu().bank(7); }
};

void test_register_widths() {
  System s;
  // IPRA is 16-bit: byte lanes compose big-endian.
  s.reg16(kIpra, 0x1234);
  CHECK_EQ(s.reg8(kIpra), 0x12u);
  CHECK_EQ(s.reg8(kIpra + 1), 0x34u);
  s.reg8(kIpra + 1, 0x56);
  CHECK_EQ(s.reg16(kIpra), 0x1256u);
  // TCOR0 is 32-bit.
  s.reg32(kTcor0, 0xDEADBEEFu);
  CHECK_EQ(s.reg16(kTcor0 + 2), 0xBEEFu);
  CHECK_EQ(s.reg8(kTcor0), 0xDEu);
  // The reset value of ICR1 selects the IRL mode.
  CHECK_EQ(s.reg16(kIcr1), 0x4000u);
}

void test_tmu() {
  System s;
  s.reg16(kIpra, 0xF000);  // TMU0 priority 15
  s.reg32(kTcor0, 99);
  s.reg32(kTcnt0, 99);
  s.reg16(kTcr0, 0x20);    // UNIE, P/4
  s.reg8(kTstr, 1);
  // 100 counts at 4 * 4 = 16 states each (FRQCR 0103: peripheral clock = CPU/4).
  s.m.run(1500);
  CHECK_EQ(s.handled(), 0u);
  CHECK_EQ(s.m.periph().tmu_count(0), 99u - 1500u / 16u);
  s.m.run(200);
  CHECK(s.handled() >= 1u);
  CHECK(s.reg16(kTcr0) & 0x100);
  CHECK_EQ(s.m.cpu().intevt2(), 0x400u);
  CHECK_EQ(s.m.cpu().intevt(), 0x400u);
  CHECK_EQ(s.reg32(kIntevt2), 0x400u);
  // The level stays until UNF is cleared: the handler above does not, so it
  // re-enters after RTE; clear it from the outside and count again.
  const u32 count = s.handled();
  s.reg16(kTcr0, 0x20);
  s.m.run(1700);
  CHECK(s.handled() > count);
}

void test_irq_pins_edge_and_level() {
  System s;
  s.reg16(kIcr1, 0x0000);  // independent IRQ pins, all falling-edge
  s.reg16(kIprc, 0x00F0);  // IRQ1 priority 15
  s.m.periph().set_irq_pin(1, false);
  s.m.run(50);
  CHECK(s.handled() >= 1u);
  CHECK_EQ(s.m.cpu().intevt2(), 0x620u);
  CHECK_EQ(s.m.cpu().intevt(), 0x200u);  // level code of priority 15
  CHECK_EQ(s.reg8(kIrr0) & 0x02, 0x02u);
  // The latch holds the request until IRR0 is written.
  s.m.periph().set_irq_pin(1, true);
  const u32 before = s.handled();
  s.m.run(50);
  CHECK(s.handled() > before);
  s.reg8(kIrr0, u8(s.reg8(kIrr0) & ~0x02));
  s.m.run(50);  // a handler entered just before the clear still finishes
  const u32 cleared = s.handled();
  s.m.run(50);
  CHECK_EQ(s.handled(), cleared);
  // Level sensing: the request follows the pin.
  s.reg16(kIcr1, 0x0008);  // IRQ1 sense = low level
  s.m.periph().set_irq_pin(1, false);
  s.m.run(50);
  CHECK(s.handled() > cleared);
  s.m.periph().set_irq_pin(1, true);
  const u32 released = s.handled();
  s.m.run(50);
  CHECK_EQ(s.handled(), released);
  CHECK_EQ(s.reg8(kIrr0) & 0x02, 0u);
}

void test_pint_pins() {
  System s;
  s.reg16(kIprd, 0x6000);    // PINT0-7 priority 6
  s.reg16(kPinter, 0x0002);  // PINT1 enabled, low level requests (ICR2 = 0)
  s.m.run(50);
  CHECK_EQ(s.handled(), 0u);  // the pin idles high
  s.m.periph().set_pint_pin(1, false);
  s.m.run(50);
  CHECK(s.handled() >= 1u);
  CHECK_EQ(s.m.cpu().intevt2(), 0x700u);
  CHECK_EQ(s.reg8(kIrr0) & 0xC0, 0x80u);
  // Masked by PINTER, the request goes away with the pin still low.
  s.reg16(kPinter, 0x0000);
  const u32 masked = s.handled();
  s.m.run(50);
  CHECK_EQ(s.handled(), masked);
  CHECK_EQ(s.reg8(kIrr0) & 0xC0, 0u);
  // ICR2 flips the requesting level; PINT8-15 is the other source.
  s.m.periph().set_pint_pin(1, true);
  s.reg16(kIprd, 0x0600);
  s.reg16(kIcr2, 0x0100);
  s.reg16(kPinter, 0x0100);  // PINT8 idles high and now requests
  s.m.run(50);
  CHECK(s.handled() > masked);
  CHECK_EQ(s.m.cpu().intevt2(), 0x720u);
  CHECK_EQ(s.reg8(kIrr0) & 0xC0, 0x40u);
  s.m.periph().set_pint_pin(8, false);
  const u32 released = s.handled();
  s.m.run(50);
  CHECK_EQ(s.handled(), released);
}

void test_irl_mode_and_mask() {
  System s;
  s.m.periph().set_irl_level(3);  // priority 12, INTEVT 0x200 + 3 * 0x20
  s.m.run(50);
  CHECK(s.handled() >= 1u);
  CHECK_EQ(s.m.cpu().intevt2(), 0x260u);
  CHECK_EQ(s.m.cpu().intevt(), 0x260u);
  s.m.periph().set_irl_level(15);
  const u32 before = s.handled();
  s.m.run(50);
  CHECK_EQ(s.handled(), before);
  // Masked by IMASK = 12: level 3 (priority 12) is not accepted, level 1 is.
  s.m.cpu().set_sr(kSrMD | (12u << kSrIShift));
  s.m.periph().set_irl_level(3);
  s.m.run(50);
  CHECK_EQ(s.handled(), before);
  s.m.periph().set_irl_level(1);
  s.m.run(50);
  CHECK(s.handled() > before);
  CHECK_EQ(s.m.cpu().intevt2(), 0x220u);
}

void test_nmi() {
  System s;
  s.m.cpu().set_sr(kSrMD | kSrIMask);  // everything maskable masked
  s.m.periph().set_nmi_pin(false);     // falling edge (NMIE = 0)
  s.m.run(50);
  CHECK_EQ(s.handled(), 1u);           // edge-latched: once
  CHECK_EQ(s.m.cpu().intevt2(), 0x1C0u);
  CHECK(s.reg16(kIcr0) & 0x8000 ? false : true);  // NMIL low
}

void test_sci_receive() {
  System s;
  s.reg8(kScscr, 0x50);   // RIE, RE
  s.reg16(kIprb, 0x00F0);  // SCI priority 15
  s.m.periph().set_sci_byte_states(1000);
  const u8 bytes[2] = {0x90, 0x3C};
  s.m.periph().sci_receive(bytes, 2);
  s.m.run(900);
  CHECK_EQ(s.handled(), 0u);
  s.m.run(200);
  CHECK(s.handled() >= 1u);
  CHECK_EQ(s.m.cpu().intevt2(), 0x500u);
  CHECK_EQ(s.reg8(kScrdr), 0x90u);
  CHECK(s.reg8(kScssr) & 0x40);
  // The second byte waits until RDRF is cleared.
  s.m.run(2000);
  CHECK_EQ(s.reg8(kScrdr), 0x90u);
  s.reg8(kScssr, 0x84);  // clear RDRF
  s.m.run(1100);
  CHECK_EQ(s.reg8(kScrdr), 0x3Cu);
}

void test_scif_receive() {
  System s;
  s.reg8(kScscr2, 0x50);   // RIE, RE
  s.reg16(kIpre, 0x00F0);  // SCIF priority 15
  s.m.periph().set_scif_byte_states(1, 1000);
  const u8 bytes[2] = {0x90, 0x3C};
  s.m.periph().scif_receive(1, bytes, 2);
  s.m.run(1100);
  CHECK(s.handled() >= 1u);
  CHECK_EQ(s.m.cpu().intevt2(), 0x920u);
  CHECK_EQ(s.reg16(kScfdr2), 1u);
  s.m.run(1000);
  CHECK_EQ(s.reg16(kScfdr2), 2u);
  CHECK_EQ(s.reg8(kScfrdr2), 0x90u);
  CHECK_EQ(s.reg16(kScfdr2), 1u);
  CHECK_EQ(s.reg8(kScfrdr2), 0x3Cu);
  CHECK_EQ(s.reg16(kScfdr2), 0u);
  s.reg16(kScssr2, 0x0000);  // clear RDF
  s.m.run(50);
  const u32 before = s.handled();
  s.m.run(100);
  CHECK_EQ(s.handled(), before);
}

// Without a forced byte time the SCI takes it from SMR / BRR: 8N1 at CKS 0
// is 10 bits of 32 * (N + 1) peripheral clocks, 4 states each at reset.
void test_sci_frame_from_brr() {
  System s;
  s.reg8(0xFFFFFE82u, 2);  // BRR = 2: a bit is 32 * 3 * 4 = 384 states, a frame 3840
  s.reg8(kScscr, 0x10);    // RE
  CHECK_EQ(s.m.periph().sci_frame_states(), 3840u);
  s.m.periph().sci_receive(0x42);
  s.m.run(3700);
  CHECK(!(s.reg8(kScssr) & 0x40));
  s.m.run(200);
  CHECK(s.reg8(kScssr) & 0x40);
  CHECK_EQ(s.reg8(kScrdr), 0x42u);
  // A board forcing a byte time overrides the generator.
  s.m.periph().set_sci_byte_states(100);
  CHECK_EQ(s.m.periph().sci_frame_states(), 100u);
  // 7 bits, parity, 2 stop bits, CKS 1: 11 bits of 32 * 4 * (N + 1) * 4.
  s.reg8(0xFFFFFE80u, 0x69);
  s.reg8(0xFFFFFE82u, 0);
  s.m.periph().set_sci_byte_states(0);
  CHECK_EQ(s.m.periph().sci_frame_states(), 11u * 512u);
  // The SCIF reads its own registers.
  s.reg8(0xA4000152u, 1);  // SCBRR2 = 1
  CHECK_EQ(s.m.periph().scif_frame_states(1), 10u * 256u);
}

// A timer nobody listens to free-runs without scheduler events, and its
// count is right whenever it is read: a 10-count period at 16 states each,
// read after 1,000,000 states (62,500 counts), is exactly where it started.
void test_tmu_free_running() {
  System s;
  s.reg32(kTcor0, 9);
  s.reg32(kTcnt0, 9);
  s.reg16(kTcr0, 0x00);  // UNIE clear, P/4
  s.reg8(kTstr, 1);
  const size_t events = s.m.sched().pending();
  s.m.run(1000000);
  CHECK_EQ(s.m.sched().pending(), events);
  CHECK_EQ(s.handled(), 0u);
  CHECK_EQ(s.m.periph().tmu_count(0), 9u);
  CHECK(s.reg16(kTcr0) & 0x100);  // UNF
  s.m.run(16 * 3);
  CHECK_EQ(s.m.periph().tmu_count(0), 6u);
  // Enabling the interrupt arms the event and the handler runs.
  s.reg16(kIpra, 0xF000);
  s.reg16(kTcr0, 0x20);  // UNIE, UNF cleared by writing 0
  s.m.run(16 * 10 + 50);
  CHECK(s.handled() >= 1u);
}

void test_watchdog_interval() {
  System s;
  s.reg16(kIprb, 0xF000);   // WDT priority 15
  s.reg16(kWtcsr, 0xA580);  // TME, interval mode, P/2: 256 counts of 8 states
  s.m.run(2000);
  CHECK_EQ(s.handled(), 0u);
  s.m.run(200);
  CHECK(s.handled() >= 1u);
  CHECK_EQ(s.m.cpu().intevt2(), 0x560u);
  CHECK(s.reg8(kWtcsr) & 0x08);
}

void test_refresh_counter() {
  System s;
  s.reg16(kRtcor, 0xA50A);  // password H'A5, RTCOR = 10
  s.reg16(kRtcsr, 0xA508);  // CKS = B/4: a match every 40 states (bus clock = CPU clock at reset)
  s.m.run(500);
  CHECK_EQ(s.reg16(kRfcr), 12u);
  CHECK(s.reg16(kRtcsr) & 0x80);
  // Many periods later the counter wraps at its limit and OVF is set, with
  // no event spent on it (CMIE and OVIE are clear).
  const size_t events = s.m.sched().pending();
  s.m.run(40 * 1030);
  CHECK_EQ(s.m.sched().pending(), events);
  CHECK_EQ(s.reg16(kRfcr), (12u + 1030u) % 1024u);
  CHECK(s.reg16(kRtcsr) & 0x04);
}

void test_dma_auto_request() {
  System s;
  s.reg16(kIpre, 0xF000);  // DMAC priority 15
  for (u32 i = 0; i < 4; ++i) s.reg32(kRam + 0x100 + i * 4, 0x11110000u + i);
  s.reg32(kSar0, kRam + 0x100);
  s.reg32(kDar0, kRam + 0x200);
  s.reg32(kDmatcr0, 4);
  s.reg16(kDmaor, 0x0001);  // DME
  s.reg32(kChcr0, 0x00005425u);  // dst/src increment, auto request, longword, IE, DE
  s.m.run(50);
  for (u32 i = 0; i < 4; ++i) CHECK_EQ(s.reg32(kRam + 0x200 + i * 4), 0x11110000u + i);
  CHECK(s.reg32(kChcr0) & 0x2);  // TE
  CHECK(s.handled() >= 1u);
  CHECK_EQ(s.m.cpu().intevt2(), 0x800u);
}

void test_ports() {
  System s;
  unsigned out_port = 99;
  u8 out_data = 0;
  s.m.periph().set_port_hooks([](unsigned port) { return port == 1 ? u8(0x3C) : u8(0xFF); },
                              [&](unsigned port, u8 data, u16) { out_port = port; out_data = data; });
  s.reg16(kPacr, 0x5555);  // port A: all outputs
  s.reg8(kPadr, 0xA5);
  CHECK_EQ(out_port, 0u);
  CHECK_EQ(out_data, 0xA5u);
  CHECK_EQ(s.reg8(kPadr), 0xA5u);
  s.reg16(kPbcr, 0xFFFF);  // port B: all inputs
  CHECK_EQ(s.reg8(kPbdr), 0x3Cu);
}

void test_adc() {
  System s;
  s.m.periph().set_adc_input(2, 0x2AB);
  s.reg8(kAdcsr, 0x22);  // ADST, channel 2
  s.m.run(536 * 4 + 10);
  CHECK(s.reg8(kAdcsr) & 0x80);
  CHECK(!(s.reg8(kAdcsr) & 0x20));
  CHECK_EQ(s.reg8(kAddrch), 0xAAu);
  CHECK_EQ(s.reg8(kAddrcl), 0xC0u);
}

}  // namespace

int main() {
  test_register_widths();
  test_tmu();
  test_irq_pins_edge_and_level();
  test_pint_pins();
  test_irl_mode_and_mask();
  test_nmi();
  test_sci_receive();
  test_scif_receive();
  test_sci_frame_from_brr();
  test_tmu_free_running();
  test_watchdog_interval();
  test_refresh_counter();
  test_dma_auto_request();
  test_ports();
  test_adc();
  return test::finish("sh3_test_periph");
}

// Interrupt controller tests (H8/510 manual section 5).
#include "cpu/h8500/machine.hpp"
#include "common/test_util.hpp"

using namespace h8500;

namespace {

struct Board {
  Machine m;
  Board() : m(ChipModel::H8_510, 2) { m.bus().map_ram(0x0000, 0xFE80, BusClass::W16_S2); }
  Cpu& cpu() { return m.cpu(); }
  Intc& intc() { return m.intc(); }
  void poke(u32 addr, std::initializer_list<u8> bytes) {
    u32 a = addr;
    for (u8 b : bytes) m.bus().mem()[a++] = b;
  }
  void poke16(u32 addr, u16 v) { Bus::put_be16(m.bus().mem() + addr, v); }
  u16 peek16(u32 addr) { return Bus::be16(m.bus().mem() + addr); }

  // Reset with a NOP loop at H'0100.  Every vector H'10..H'7F points at its
  // own handler which writes the vector address to H'F100 (the marker) and
  // returns, so tests can see which interrupt ran.
  void start(u8 mask) {
    poke16(0, 0x0100);
    poke(0x0100, {0x00, 0x00, 0x20, 0xFC});  // NOP NOP BRA -4
    for (u32 v = 0x10; v < 0x80; v += 2) {
      poke16(v, handler(u8(v)));
      // MOV:I #v,R0 ; MOV.W R0,@H'F100:16 ; RTE
      poke(handler(u8(v)), {0x58, 0x00, u8(v), 0x1D, 0xF1, 0x00, 0x90, 0x0A});
    }
    m.cpu().invalidate_all();
    m.reset();
    cpu().regs().r[7] = 0xF000;
    cpu().regs().sr = u16((cpu().regs().sr & ~Cpu::kMaskBits) | (u16(mask) << Cpu::kMaskShift));
  }
  static u16 handler(u8 vector_addr) { return u16(0x0500 + (vector_addr - 0x10) * 4); }
  u16 marker() { return peek16(0xF100); }
  void clear_marker() { poke16(0xF100, 0); }

  // Run until a handler has written the marker (or `max` states pass).
  // Returns the vector address seen, 0 if none.
  u16 wait_vector(u64 max = 200) {
    clear_marker();
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    return marker();
  }
};

void test_registers() {
  Board b;
  b.start(7);
  Bus& bus = b.m.bus();
  CHECK_EQ(bus.read8(0xFF00), 0x00);   // IPRA
  CHECK_EQ(bus.read8(0xFF08), 0x00);   // DTEA
  CHECK_EQ(bus.read8(0xFF1C), 0xFE);   // NMICR
  CHECK_EQ(bus.read8(0xFF1D), 0xF0);   // IRQCR
  CHECK_EQ(bus.read8(0xFF04), 0xFF);   // unassigned register
  bus.write8(0xFF00, 0xFF);
  CHECK_EQ(bus.read8(0xFF00), 0x77);   // bits 7 and 3 always 0
  bus.write8(0xFF1C, 0x01);
  CHECK_EQ(bus.read8(0xFF1C), 0xFF);
  bus.write8(0xFF1D, 0x05);
  CHECK_EQ(bus.read8(0xFF1D), 0xF5);
  bus.write8(0xFF0B, 0xA5);
  CHECK_EQ(bus.read8(0xFF0B), 0xA5);
  CHECK_EQ(b.intc().level_of(IrqSrc::Irq0), 7);
  CHECK_EQ(b.intc().level_of(IrqSrc::Irq1), 7);
  CHECK(b.intc().dtc_enabled(IrqSrc::Adi));
  CHECK(!b.intc().dtc_enabled(IrqSrc::Sci2Eri));  // no DTE bit for ERI

  // Access through instructions: MOV.B #H'53,@H'FF01:16 ; MOV.B @H'FF01:16,R1
  b.poke(0x0200, {0x15, 0xFF, 0x01, 0x06, 0x53, 0x15, 0xFF, 0x01, 0x81});
  b.cpu().regs().pc = 0x0200;
  b.cpu().step();
  b.cpu().step();
  CHECK_EQ(b.cpu().regs().r[1] & 0xFF, 0x53);
  CHECK_EQ(b.intc().level_of(IrqSrc::Frt1Ici), 5);
  CHECK_EQ(b.intc().level_of(IrqSrc::Frt2Ocib), 3);
}

void test_priority_levels() {
  Board b;
  b.start(0);
  Intc& ic = b.intc();
  b.m.bus().write8(0xFF01, 0x25);  // FRT1 level 2, FRT2 level 5

  // Two requests at different levels: the higher one is taken first.
  ic.set_request(IrqSrc::Frt1Ocia, true);
  ic.set_request(IrqSrc::Frt2Fovi, true);
  CHECK_EQ(b.wait_vector(), 0x5E);  // FRT2 FOVI
  CHECK_EQ(b.cpu().interrupt_mask(), 5);
  ic.set_request(IrqSrc::Frt2Fovi, false);  // handler clears the flag
  // Back in the main loop the level-2 request is taken.
  CHECK_EQ(b.wait_vector(), 0x52);
  CHECK_EQ(b.cpu().interrupt_mask(), 2);
  ic.set_request(IrqSrc::Frt1Ocia, false);
  CHECK_EQ(b.wait_vector(100), 0);
}

// A request at the same level as the current mask waits for RTE.
void test_mask_blocks_equal_level() {
  Board b;
  b.start(0);
  Intc& ic = b.intc();
  b.m.bus().write8(0xFF01, 0x05);  // FRT2 level 5
  b.m.bus().write8(0xFF02, 0x50);  // 8-bit timer level 5
  ic.set_request(IrqSrc::Frt2Fovi, true);
  CHECK_EQ(b.wait_vector(), 0x5E);
  // Inside the FRT2 handler (mask 5) a level-5 timer request is pending.
  ic.set_request(IrqSrc::TmrCmia, true);
  ic.set_request(IrqSrc::Frt2Fovi, false);
  b.cpu().step();  // RTE (mask 0)
  CHECK_EQ(b.cpu().interrupt_mask(), 0);
  CHECK_EQ(b.cpu().exceptions_taken(), 1u);
  b.cpu().step();  // the instruction after RTE always executes (a 2-state NOP:
                   // the new mask is not effective yet, 4.8.1 note)
  CHECK_EQ(b.cpu().exceptions_taken(), 1u);
  b.cpu().step();  // second NOP, then the timer interrupt
  CHECK_EQ(b.cpu().exceptions_taken(), 2u);
  CHECK_EQ(b.cpu().regs().pc, Board::handler(0x60));
}

void test_same_level_ordering() {
  Board b;
  b.start(0);
  Intc& ic = b.intc();
  b.m.bus().write8(0xFF01, 0x05);  // FRT2 level 5
  b.m.bus().write8(0xFF02, 0x50);  // 8-bit timer level 5

  // Two modules at the same level: Table 5-2 order (FRT2 before 8-bit timer).
  ic.set_request(IrqSrc::TmrCmib, true);
  ic.set_request(IrqSrc::Frt2Ici, true);
  CHECK_EQ(b.wait_vector(), 0x58);  // FRT2 ICI
  ic.set_request(IrqSrc::Frt2Ici, false);
  CHECK_EQ(b.wait_vector(), 0x62);  // then TMR CMIB
  ic.set_request(IrqSrc::TmrCmib, false);

  // Within one module: ICI > OCIA > OCIB > FOVI.
  ic.set_request(IrqSrc::Frt2Fovi, true);
  ic.set_request(IrqSrc::Frt2Ocib, true);
  ic.set_request(IrqSrc::Frt2Ocia, true);
  CHECK_EQ(b.wait_vector(), 0x5A);  // OCIA
  ic.set_request(IrqSrc::Frt2Ocia, false);
  CHECK_EQ(b.wait_vector(), 0x5C);  // OCIB
  ic.set_request(IrqSrc::Frt2Ocib, false);
  CHECK_EQ(b.wait_vector(), 0x5E);  // FOVI
  ic.set_request(IrqSrc::Frt2Fovi, false);

  // Level 0 = masked even with mask 0.
  b.m.bus().write8(0xFF01, 0x00);
  ic.set_request(IrqSrc::Frt2Ici, true);
  CHECK_EQ(b.wait_vector(100), 0);
  // Raising the level later lets it through.
  b.m.bus().write8(0xFF01, 0x01);
  CHECK_EQ(b.wait_vector(), 0x58);
}

void test_irq_pins() {
  Board b;
  b.start(0);
  Intc& ic = b.intc();
  b.m.bus().write8(0xFF00, 0x63);  // IRQ0 level 6, IRQ1-3 level 3

  // Disabled pins do nothing.
  ic.set_irq_pin(1, true);
  ic.set_irq_pin(1, false);
  ic.set_irq_pin(0, true);
  CHECK_EQ(b.wait_vector(50), 0);

  // IRQ1: edge-latched.  Enable, pulse low: the request stays until accepted.
  b.m.bus().write8(0xFF1D, 0x02);
  ic.set_irq_pin(1, true);
  ic.set_irq_pin(1, false);
  CHECK(ic.request(IrqSrc::Irq1));
  CHECK_EQ(b.wait_vector(), 0x48);
  CHECK(!ic.request(IrqSrc::Irq1));  // cleared on acceptance
  CHECK_EQ(b.wait_vector(100), 0);   // no retrigger
  // Holding the pin low does not retrigger; a new falling edge does.
  ic.set_irq_pin(1, true);
  CHECK_EQ(b.wait_vector(), 0x48);
  CHECK_EQ(b.wait_vector(100), 0);
  ic.set_irq_pin(1, false);
  ic.set_irq_pin(1, true);
  CHECK_EQ(b.wait_vector(), 0x48);
  ic.set_irq_pin(1, false);
  // Disabling the pin drops a latched request.
  ic.set_irq_pin(1, true);
  CHECK(ic.request(IrqSrc::Irq1));
  b.m.bus().write8(0xFF1D, 0x00);
  CHECK(!ic.request(IrqSrc::Irq1));
  ic.set_irq_pin(1, false);

  // IRQ0: level-sensed.  While the pin is low it is taken again after RTE.
  b.m.bus().write8(0xFF1D, 0x01);  // pin 0 already low: enabling requests
  CHECK(ic.request(IrqSrc::Irq0));
  CHECK_EQ(b.wait_vector(), 0x40);
  CHECK_EQ(b.wait_vector(), 0x40);
  ic.set_irq_pin(0, false);
  CHECK(!ic.request(IrqSrc::Irq0));
  b.m.run(50);  // let the current handler return
  CHECK_EQ(b.wait_vector(100), 0);
}

void test_nmi() {
  Board b;
  b.start(7);  // everything masked: only NMI gets through
  Intc& ic = b.intc();
  ic.set_nmi_pin(true);
  ic.set_nmi_pin(false);   // falling edge (NMIEG = 0)
  CHECK_EQ(b.wait_vector(), 0x16);
  CHECK_EQ(b.cpu().interrupt_mask(), 7);
  CHECK_EQ(b.cpu().exceptions_taken(), 1u);
  ic.set_nmi_pin(true);    // rising edge: ignored
  CHECK_EQ(b.wait_vector(50), 0);
  b.m.bus().write8(0xFF1C, 0x01);  // NMIEG = 1: rising edge
  ic.set_nmi_pin(false);
  CHECK_EQ(b.wait_vector(50), 0);
  ic.set_nmi_pin(true);
  CHECK_EQ(b.wait_vector(), 0x16);
  CHECK_EQ(b.cpu().exceptions_taken(), 2u);
}

// A source whose DTE bit is set goes to the DTC client if one is attached.
struct FakeDtc : DtcClient {
  int calls = 0;
  IrqSrc last = IrqSrc::Count;
  u8 vec = 0;
  bool dtc_request(IrqSrc src, u8 vector) override { ++calls; last = src; vec = vector; return true; }
};

void test_dte_routing() {
  Board b;
  b.start(0);
  Intc& ic = b.intc();
  FakeDtc dtc;
  b.m.bus().write8(0xFF01, 0x40);  // FRT1 level 4
  // Without a DTE bit the request is served by the CPU.
  ic.set_request(IrqSrc::Frt1Ocia, true);
  CHECK_EQ(b.wait_vector(), 0x52);
  ic.set_request(IrqSrc::Frt1Ocia, false);
  b.m.run(50);
  // With the DTE bit set the machine's DTC takes it (register table left at
  // zero: a harmless transfer) and the CPU is not interrupted.
  b.m.bus().write8(0xFF09, 0x20);  // DTEB bit 5: FRT1 OCIA -> DTC
  ic.set_request(IrqSrc::Frt1Ocia, true);
  CHECK_EQ(b.wait_vector(100), 0);
  CHECK(b.m.dtc().transfers() >= 1);
  ic.set_request(IrqSrc::Frt1Ocia, false);
  // A substitute client receives the request instead.
  ic.set_dtc_client(&dtc);
  const u64 exc = b.cpu().exceptions_taken();
  ic.set_request(IrqSrc::Frt1Ocia, true);
  CHECK_EQ(b.wait_vector(100), 0);
  CHECK_EQ(dtc.calls, 1);
  CHECK_EQ(int(dtc.last), int(IrqSrc::Frt1Ocia));
  CHECK_EQ(dtc.vec, 0x52 / 2);
  CHECK_EQ(b.cpu().exceptions_taken(), exc);
  // A source without a DTE bit still reaches the CPU.
  ic.set_request(IrqSrc::Frt1Ocia, false);
  ic.set_request(IrqSrc::Frt1Fovi, true);
  CHECK_EQ(b.wait_vector(), 0x56);
}

}  // namespace

int main() {
  test_registers();
  test_priority_levels();
  test_mask_blocks_equal_level();
  test_same_level_ordering();
  test_irq_pins();
  test_nmi();
  test_dte_routing();
  return test::finish("test_intc");
}

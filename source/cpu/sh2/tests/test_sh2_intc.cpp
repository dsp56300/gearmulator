// SH7014 interrupt controller (manual section 6).
#include "cpu/sh2/machine.hpp"
#include "common/test_util.hpp"

using namespace sh2;

namespace {

struct System {
  Machine m;
  static constexpr u32 kRam = 0xFFFFF000u;
  System() : m(ChipModel::SH7014, 1) {
    m.bus().map_ram(0x00000000, 0x10000, Bus::kClsCs0);
    // Vectors: every interrupt handler writes its vector number to H'F100 and returns.
    for (u32 v = 64; v < 160; ++v) {
      const u32 h = 0x1000 + (v - 64) * 16;
      m.bus().write32(v * 4, h);
      // MOV #v,R0 (v < 128 only fits in 8-bit imm as unsigned <= 127; use the low byte and sign):
      // MOV.W @(2,PC),R0 ; MOV.W R0,@(disp,GBR)... keep it simple: MOV #(v & 0x7F),R0 ; MOV.L R0,@R1 ; RTE ; NOP
      const u16 code[] = {u16(0xE000 | (v & 0x7F)), 0x2102, 0x002B, 0x0009};
      u32 a = h;
      for (u16 w : code) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    }
    // Main program: NOP loop at on-chip RAM.
    const u16 loop[] = {0x0009, 0xAFFD, 0x0009};  // NOP ; BRA -6 (back to NOP) ; slot NOP
    u32 a = kRam;
    for (u16 w : loop) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    m.bus().write32(0, kRam);
    m.bus().write32(4, 0xFFFFFBF0u);  // top of the 3 KB on-chip RAM
    m.cpu().invalidate_all();
    m.reset();
    m.bus().write16(0xFFFF8624, 0x0000);  // WCR1: zero wait states (reset gives 15 to every area)
    m.cpu().regs().sr &= ~Cpu::kIMask;
    m.cpu().regs().r[1] = 0xF100;
    m.bus().write32(0xF100, 0);
  }
  Bus& bus() { return m.bus(); }
  Intc& intc() { return m.intc(); }
  u32 marker() { return bus().read32(0xF100); }
  u32 wait_vector(u64 max) {
    bus().write32(0xF100, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    return marker();
  }
};

void test_registers() {
  System s;
  Bus& b = s.bus();
  CHECK_EQ(b.read16(0xFFFF8348), 0x0000);
  b.write16(0xFFFF8348, 0xABCD);
  CHECK_EQ(b.read16(0xFFFF8348), 0xABCD);
  CHECK_EQ(b.read8(0xFFFF8349), 0xCD);
  b.write8(0xFFFF8356, 0x50);  // IPRH high byte
  CHECK_EQ(b.read16(0xFFFF8356), 0x5000);
  CHECK_EQ(b.read16(0xFFFF8358), 0x8000);  // ICR: NMI pin high
  b.write16(0xFFFF8358, 0x01F3);
  CHECK_EQ(b.read16(0xFFFF8358), 0x81F3);
  CHECK_EQ(b.read16(0xFFFF835A), 0x0000);
}

void test_module_priority_and_mask() {
  System s;
  Bus& b = s.bus();
  // CMT0 level 5 (IPRG 7:4), TGI0A level 9 (IPRD 15:12).
  b.write16(0xFFFF8354, 0x0050);
  b.write16(0xFFFF834E, 0x9000);
  s.intc().set_request(IrqSrc::Cmi0, true);
  s.intc().set_request(IrqSrc::Tgi0a, true);
  CHECK_EQ(s.wait_vector(100), 88u & 0x7F);  // TGI0A first (higher level)
  CHECK_EQ(s.m.cpu().interrupt_mask(), 9u);
  s.intc().set_request(IrqSrc::Tgi0a, false);
  // After RTE the mask drops and CMT0 (level 5) comes in.
  CHECK_EQ(s.wait_vector(100), 144u & 0x7F);
  s.intc().set_request(IrqSrc::Cmi0, false);
  // Level 0 = masked: no interrupt.
  b.write16(0xFFFF8354, 0x0000);
  s.intc().set_request(IrqSrc::Cmi0, true);
  CHECK_EQ(s.wait_vector(200), 0u);
  s.intc().set_request(IrqSrc::Cmi0, false);
  // Equal levels: default order (TGI0A before TCI0V, both level 3).
  b.write16(0xFFFF834E, 0x3300);
  s.intc().set_request(IrqSrc::Tci0v, true);
  s.intc().set_request(IrqSrc::Tgi0a, true);
  CHECK_EQ(s.wait_vector(100), 88u & 0x7F);
  s.intc().set_request(IrqSrc::Tgi0a, false);
  CHECK_EQ(s.wait_vector(100), 92u & 0x7F);
  s.intc().set_request(IrqSrc::Tci0v, false);
}

void test_irq_pins() {
  System s;
  Bus& b = s.bus();
  b.write16(0xFFFF8348, 0x7000);  // IRQ0 level 7
  b.write16(0xFFFF834A, 0x0004);  // IRQ6 level 4? no: IPRB bits 7-4 = IRQ6 -> 0x0040
  b.write16(0xFFFF834A, 0x0040);
  // Level sense (default): the request follows the pin.
  s.intc().set_irq_pin(0, true);
  CHECK_EQ(b.read16(0xFFFF835A), 0x0080);
  CHECK_EQ(s.wait_vector(100), 64u & 0x7F);
  // Still low after the handler: taken again.
  CHECK_EQ(s.wait_vector(100), 64u & 0x7F);
  s.intc().set_irq_pin(0, false);
  CHECK_EQ(b.read16(0xFFFF835A), 0x0000);
  CHECK_EQ(s.wait_vector(100), 0u);
  // Edge sense on IRQ6: latched on the falling edge, cleared by acceptance.
  b.write16(0xFFFF8358, 0x0002);  // IRQ6S = edge
  s.intc().set_irq_pin(6, true);
  CHECK_EQ(b.read16(0xFFFF835A), 0x0002);
  s.intc().set_irq_pin(6, false);
  CHECK_EQ(b.read16(0xFFFF835A), 0x0002);  // still latched
  CHECK_EQ(s.wait_vector(100), 70u & 0x7F);
  CHECK_EQ(b.read16(0xFFFF835A), 0x0000);  // withdrawn by acceptance
  CHECK_EQ(s.wait_vector(100), 0u);
  // Latched again and withdrawn by software: read 1, write 0.
  s.intc().set_irq_pin(6, true);
  s.intc().set_irq_pin(6, false);
  s.m.cpu().regs().sr |= Cpu::kIMask;  // hold it off
  CHECK_EQ(b.read16(0xFFFF835A), 0x0002);
  b.write16(0xFFFF835A, 0x0000);
  CHECK_EQ(b.read16(0xFFFF835A), 0x0000);
  s.m.cpu().regs().sr &= ~Cpu::kIMask;
  CHECK_EQ(s.wait_vector(100), 0u);
}

void test_nmi() {
  System s;
  s.bus().write32(11 * 4, 0x1000);  // reuse handler 0 (writes 64 & 0x7F = 0... use marker check on mask)
  s.intc().set_nmi_pin(false);      // falling edge (NMIE = 0)
  s.m.run(1);                       // the poll takes the NMI, then one handler instruction
  CHECK_EQ(s.m.cpu().interrupt_mask(), 15u);
  CHECK_EQ(s.m.cpu().regs().pc >= 0x1000u && s.m.cpu().regs().pc < 0x1010u, true);
  // Rising edge does nothing with NMIE = 0.
  s.m.cpu().regs().sr &= ~Cpu::kIMask;
  s.m.run(20);  // RTE etc.
  const u64 exc = s.m.cpu().exceptions_taken();
  s.intc().set_nmi_pin(true);
  s.m.run(20);
  CHECK_EQ(s.m.cpu().exceptions_taken(), exc);
  s.bus().write16(0xFFFF8358, 0x0100);  // NMIE = rising
  s.intc().set_nmi_pin(false);
  s.m.run(20);
  CHECK_EQ(s.m.cpu().exceptions_taken(), exc);
  s.intc().set_nmi_pin(true);
  s.m.run(20);
  CHECK_EQ(s.m.cpu().exceptions_taken(), exc + 1);
}

void test_dma_route_masks_cpu() {
  System s;
  struct Client final : DmaRequestClient {
    int hits = 0;
    void dma_request(IrqSrc) override { ++hits; }
  } client;
  s.intc().set_dma_client(&client);
  s.bus().write16(0xFFFF8352, 0x0070);  // SCI0 level 7
  s.intc().set_dma_route(IrqSrc::Rxi0, true);
  s.intc().set_request(IrqSrc::Rxi0, true);
  CHECK_EQ(client.hits, 1);
  CHECK_EQ(s.wait_vector(100), 0u);  // not presented to the CPU
  s.intc().set_dma_route(IrqSrc::Rxi0, false);
  CHECK_EQ(s.wait_vector(100), 129u & 0x7F);
}

}  // namespace

int main() {
  test_registers();
  test_module_priority_and_mask();
  test_irq_pins();
  test_nmi();
  test_dma_route_masks_cpu();
  return test::finish("test_sh2_intc");
}

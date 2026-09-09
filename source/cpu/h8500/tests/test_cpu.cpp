// CPU execution / timing smoke tests.
#include <cstring>
#include <type_traits>
#include <vector>

#include "cpu/h8500/cpu.hpp"
#include "common/test_util.hpp"

using namespace h8500;

namespace {

// Minimal H8/510-like system in minimum mode: 64 KB of 16-bit 2-state RAM
// (covering vectors, code and data); tests remap parts of it to slower classes.
struct System {
  ChipConfig cfg;
  Bus bus;
  Cpu cpu;

  explicit System(ChipModel model = ChipModel::H8_510, u8 mode = 2)
      : cfg(make_chip_config(model, mode)), bus(mode >= 3 ? 24 : 16), cpu(bus, cfg) {
    bus.map_ram(0x0000, 0x10000, BusClass::W16_S2);
  }

  u8* ram() { return bus.mem(); }
  void poke(u32 addr, std::initializer_list<u8> bytes) {
    u32 a = addr;
    for (u8 b : bytes) ram()[a++] = b;
  }
  void poke16(u32 addr, u16 v) { ram()[addr] = u8(v >> 8); ram()[addr + 1] = u8(v); }
  u16 peek16(u32 addr) const { return Bus::be16(bus.mem() + addr); }

  // Reset with PC = start, SP = sp.  Pokes bypass the bus, so drop any cells
  // decoded from earlier contents first.
  void start(u16 pc, u16 sp = 0xFE00) {
    poke16(0x0000, pc);
    cpu.invalidate_all();
    cpu.reset();
    cpu.regs().r[7] = sp;
  }
};

void test_reset_and_nop() {
  System s;
  s.poke(0x0100, {0x00, 0x00});
  s.start(0x0100);
  CHECK_EQ(s.cpu.regs().pc, 0x0100);
  CHECK_EQ(s.cpu.interrupt_mask(), 7);
  CHECK_EQ(s.cpu.step(), 2u);  // NOP: 2 states
  CHECK_EQ(s.cpu.regs().pc, 0x0101);
}

// Manual 2.6.5 example 1: ADD.W @R0,R1 fetched from 16-bit 2-state memory
// takes 6 states at an even address, 5 at an odd one.
void test_add_timing_parity() {
  System s;
  s.poke(0x0100, {0xD8, 0x21});
  s.poke(0x0201, {0xD8, 0x21});
  s.poke16(0x0300, 0x1234);
  s.start(0x0100);
  s.cpu.regs().r[0] = 0x0300;
  s.cpu.regs().r[1] = 0x0001;
  CHECK_EQ(s.cpu.step(), 6u);
  CHECK_EQ(s.cpu.regs().r[1], 0x1235);

  s.start(0x0201);
  s.cpu.regs().r[0] = 0x0300;
  s.cpu.regs().r[1] = 0x0001;
  CHECK_EQ(s.cpu.step(), 5u);
}

// Manual 2.6.5 example 2: JSR @R0 with the stack in an 8-bit 3-state area:
// 9 + 0 + 2*2 = 13 states (even), 14 (odd).
void test_jsr_timing_slow_stack() {
  System s;
  s.bus.map_ram(0xC000, 0x1000, BusClass::W8_S3);  // stack area
  s.poke(0x0100, {0x11, 0xD8});
  s.poke(0x0201, {0x11, 0xD8});
  s.start(0x0100, 0xC800);
  s.cpu.regs().r[0] = 0x2000;
  CHECK_EQ(s.cpu.step(), 13u);
  CHECK_EQ(s.cpu.regs().pc, 0x2000);
  CHECK_EQ(s.cpu.regs().r[7], 0xC7FE);
  CHECK_EQ(s.peek16(0xC7FE), 0x0102);

  s.start(0x0201, 0xC800);
  s.cpu.regs().r[0] = 0x2000;
  CHECK_EQ(s.cpu.step(), 14u);
}

// Manual 2.6.5 example 3: instruction fetched from an 8-bit 3-state area,
// operand in 16-bit 2-state memory: 5 + 2*(1+1) = 9.
void test_fetch_from_slow_bus() {
  System s;
  s.bus.map_ram(0x9000, 0x1000, BusClass::W8_S3);
  s.poke(0x9002, {0xD8, 0x21});  // ADD.W @R0,R1 at H'9002
  s.start(0x9002);
  s.cpu.regs().r[0] = 0x0300;
  CHECK_EQ(s.cpu.step(), 9u);
}

// Wait states: operand in a 3-state area with 2 wait states per access.
void test_wait_states() {
  System s;
  s.bus.map_ram(0xA000, 0x1000, BusClass::W16_S3, 2);
  s.poke(0x0100, {0xD8, 0x21});  // ADD.W @R0,R1
  s.start(0x0100);
  s.cpu.regs().r[0] = 0xA000;
  // 5 + 1 (even) + I/2 (16-bit 3-state word: 1) + 2 waits * 1 bus cycle = 9
  CHECK_EQ(s.cpu.step(), 9u);
}

void test_flags_and_alu() {
  System s;
  s.poke(0x0100, {
      0x50, 0x7F,        // MOV:E #H'7F,R0
      0x04, 0x01, 0x20,  // ADD.B #1,R0        -> H'80, N=1 V=1
      0x40, 0x80,        // CMP:E #H'80,R0     -> Z=1
      0xA0, 0x11,        // EXTS R0            -> H'FF80, N=1
      0x0C, 0x00, 0x80, 0x30,  // SUB.W #H'0080,R0 -> H'FF00
      0xA8, 0x1A,        // SHLL.W R0          -> H'FE00, C=1
  });
  s.start(0x0100);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[0] & 0xFF, 0x7F);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[0] & 0xFF, 0x80);
  CHECK((s.cpu.regs().sr & Cpu::kN) != 0);
  CHECK((s.cpu.regs().sr & Cpu::kV) != 0);
  CHECK((s.cpu.regs().sr & Cpu::kC) == 0);
  s.cpu.step();
  CHECK((s.cpu.regs().sr & Cpu::kZ) != 0);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[0], 0xFF80);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[0], 0xFF00);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[0], 0xFE00);
  CHECK((s.cpu.regs().sr & Cpu::kC) != 0);
}

// Memory-destination forms that carry both an EA extension and an immediate.
void test_memory_immediates() {
  System s;
  s.poke(0x0100, {
      0xE0, 0x10, 0x07, 0x12, 0x34,  // MOV.B #H'34,@(H'10,R0)   (8-bit EA, 16-bit data -> low byte)
      0xF8, 0x00, 0x20, 0x07, 0xAB, 0xCD,  // MOV.W #H'ABCD,@(H'20,R0)
      0xF8, 0x00, 0x20, 0x05, 0xAB, 0xCD,  // CMP.W #H'ABCD,@(H'20,R0)  -> Z=1
      0xE8, 0x20, 0x0D,              // ADD:Q.W #-2,@(H'20,R0)     -> H'ABCB
      0x1D, 0x20, 0x00, 0x06, 0xFF,  // MOV.W #H'FF,@H'2000:16     (8-bit data sign-extended)
      0xD0, 0xC7,                    // BSET.B #7,@R0
      0xD0, 0xF7,                    // BTST.B #7,@R0              -> Z=0
  });
  s.start(0x0100);
  s.cpu.regs().r[0] = 0x1000;
  s.cpu.step();
  CHECK_EQ(s.ram()[0x1010], 0x34);
  s.cpu.step();
  CHECK_EQ(s.peek16(0x1020), 0xABCD);
  s.cpu.step();
  CHECK((s.cpu.regs().sr & Cpu::kZ) != 0);
  s.cpu.step();
  CHECK_EQ(s.peek16(0x1020), 0xABCB);
  s.cpu.step();
  CHECK_EQ(s.peek16(0x2000), 0xFFFF);
  s.ram()[0x1000] = 0x01;
  s.cpu.step();
  CHECK_EQ(s.ram()[0x1000], 0x81);
  CHECK((s.cpu.regs().sr & Cpu::kZ) != 0);  // bit was 0 before BSET
  s.cpu.step();
  CHECK((s.cpu.regs().sr & Cpu::kZ) == 0);
}

// SCB/F counted loop from the manual: loop executes 10 times, R1 ends at 10.
void test_scb_loop() {
  System s;
  s.poke(0x0100, {
      0x58, 0x00, 0x09,  // MOV:I #9,R0
      0xA9, 0x13,        // CLR.W R1
      0xA9, 0x08,        // LO: ADD:Q.W #1,R1
      0x01, 0xB8, 0xFB,  // SCB/F R0,LO  (disp -5)
      0x00,              // NOP
  });
  s.start(0x0100);
  unsigned guard = 0;
  while (s.cpu.regs().pc != 0x010A && guard++ < 100) s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[1], 10);
  CHECK_EQ(s.cpu.regs().r[0], 0xFFFF);
}

void test_stack_ops() {
  System s;
  s.poke(0x0100, {
      0x12, 0x0F,        // STM (R0-R3),@-SP
      0xA8, 0x13, 0xA9, 0x13, 0xAA, 0x13, 0xAB, 0x13,  // CLR.W R0..R3
      0x02, 0x0F,        // LDM @SP+,(R0-R3)
      0x17, 0xFC,        // LINK FP,#-4
      0x0F,              // UNLK FP
      0x18, 0x02, 0x00,  // JSR @H'0200:16
  });
  s.poke(0x0200, {0x14, 0x02});  // RTD #2
  s.start(0x0100, 0xF000);
  for (int i = 0; i < 4; ++i) s.cpu.regs().r[i] = u16(0x1111 * (i + 1));
  const unsigned stm_states = s.cpu.step();
  CHECK_EQ(stm_states, 6u + 3 * 4);  // 6 + 3n (no EA column -> no parity adjustment)
  CHECK_EQ(s.cpu.regs().r[7], 0xEFF8);
  CHECK_EQ(s.peek16(0xEFF8), 0x1111);
  CHECK_EQ(s.peek16(0xEFFE), 0x4444);
  for (int i = 0; i < 4; ++i) s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[2], 0);
  const unsigned ldm_states = s.cpu.step();
  CHECK_EQ(ldm_states, 6u + 4 * 4);
  CHECK_EQ(s.cpu.regs().r[7], 0xF000);
  CHECK_EQ(s.cpu.regs().r[3], 0x4444);
  s.cpu.regs().r[6] = 0xABCD;
  s.cpu.step();  // LINK
  CHECK_EQ(s.cpu.regs().r[6], 0xEFFE);
  CHECK_EQ(s.cpu.regs().r[7], 0xEFFA);
  CHECK_EQ(s.peek16(0xEFFE), 0xABCD);
  s.cpu.step();  // UNLK
  CHECK_EQ(s.cpu.regs().r[6], 0xABCD);
  CHECK_EQ(s.cpu.regs().r[7], 0xF000);
  s.cpu.regs().r[7] = 0xEFFE;  // pretend an argument word was pushed
  s.cpu.step();  // JSR
  CHECK_EQ(s.cpu.regs().pc, 0x0200);
  CHECK_EQ(s.peek16(0xEFFC), 0x0112);
  s.cpu.step();  // RTD #2
  CHECK_EQ(s.cpu.regs().pc, 0x0112);
  CHECK_EQ(s.cpu.regs().r[7], 0xF000);
}

void test_trapa_and_rte() {
  System s;
  s.poke16(0x0028, 0x0400);  // TRAPA #4 vector (minimum mode: H'0028)
  s.poke(0x0100, {0x08, 0x14, 0x00});  // TRAPA #4 ; NOP
  s.poke(0x0400, {0x0A});              // RTE
  s.start(0x0100, 0xF000);
  s.cpu.regs().sr |= Cpu::kT;          // trace on, to check it is saved and cleared
  const unsigned t = s.cpu.step();
  CHECK_EQ(t, 17u);
  CHECK_EQ(s.cpu.regs().pc, 0x0400);
  CHECK_EQ(s.cpu.regs().r[7], 0xEFFC);
  CHECK_EQ(s.peek16(0xEFFC) & Cpu::kT, Cpu::kT);  // saved SR has T
  CHECK_EQ(s.peek16(0xEFFE), 0x0102);             // return address
  CHECK((s.cpu.regs().sr & Cpu::kT) == 0);
  s.cpu.step();  // RTE
  CHECK_EQ(s.cpu.regs().pc, 0x0102);
  CHECK((s.cpu.regs().sr & Cpu::kT) != 0);
  CHECK_EQ(s.cpu.regs().r[7], 0xF000);
}

// Trace: with T set, every instruction is followed by a trace exception.  RTE
// itself is not traced (manual 4.8.1: the instruction after RTE always
// executes), the instruction after it is.
void test_trace() {
  System s;
  s.poke16(0x0012, 0x0400);  // trace vector
  s.poke(0x0100, {0x00, 0x00, 0x00});  // NOP NOP NOP
  s.poke(0x0400, {0x0A});              // RTE
  s.start(0x0100, 0xF000);
  s.cpu.regs().sr |= Cpu::kT;
  s.cpu.step();  // NOP + trace exception
  CHECK_EQ(s.cpu.regs().pc, 0x0400);
  CHECK_EQ(s.cpu.exceptions_taken(), 1u);
  CHECK((s.cpu.regs().sr & Cpu::kT) == 0);
  s.cpu.step();  // RTE restores T; no trace at its own boundary
  CHECK_EQ(s.cpu.regs().pc, 0x0101);
  CHECK_EQ(s.cpu.exceptions_taken(), 1u);
  CHECK((s.cpu.regs().sr & Cpu::kT) != 0);
  s.cpu.step();  // NOP after RTE: executes, then traced
  CHECK_EQ(s.cpu.regs().pc, 0x0400);
  CHECK_EQ(s.cpu.exceptions_taken(), 2u);
  CHECK_EQ(s.peek16(0xEFFE), 0x0102);  // return address = instruction after the traced NOP
}

void test_interrupt() {
  System s;
  s.poke16(0x0040, 0x0500);  // IRQ0 vector (minimum mode H'0040)
  s.poke(0x0100, {0x00, 0x00, 0x00});
  s.poke(0x0500, {0x0A});
  s.start(0x0100, 0xF000);
  s.cpu.regs().sr &= u16(~Cpu::kMaskBits);  // mask level 0
  s.cpu.set_irq(3, 32);
  const unsigned t = s.cpu.step();  // NOP, then interrupt accepted at end of instruction
  CHECK_EQ(t, 2u + Cpu::kIrqStatesMin);
  CHECK(s.cpu.irq_accepted());
  CHECK_EQ(s.cpu.regs().pc, 0x0500);
  CHECK_EQ(s.cpu.interrupt_mask(), 3);
  CHECK_EQ(s.peek16(0xEFFE), 0x0101);
  s.cpu.set_irq(0, 0);
  s.cpu.step();  // RTE
  CHECK_EQ(s.cpu.regs().pc, 0x0101);
  CHECK_EQ(s.cpu.interrupt_mask(), 0);

  // Masked interrupt is not taken.
  s.cpu.regs().sr |= Cpu::kMaskBits;
  s.cpu.set_irq(5, 32);
  s.cpu.step();
  CHECK(!s.cpu.irq_accepted());
  CHECK_EQ(s.cpu.regs().pc, 0x0102);
}

// LDC that unmasks a pending interrupt: the following instruction executes
// before the interrupt is accepted (H8/510 manual 4.8.1).
void test_irq_deferred_after_ldc() {
  System s;
  s.poke16(0x0040, 0x0500);
  s.poke(0x0100, {
      0x0C, 0x00, 0x00, 0x88,  // LDC.W #H'0000,SR   (mask 0)
      0x58, 0x00, 0x01,        // MOV:I #1,R0        (3 states; must run before the IRQ)
      0x58, 0x00, 0x02,        // MOV:I #2,R0        (must not run before the IRQ)
  });
  s.poke(0x0500, {0x0A});
  s.start(0x0100, 0xF000);
  s.cpu.set_irq(3, 32);      // pending but masked (level 7 after reset)
  s.cpu.step();              // LDC
  CHECK(!s.cpu.irq_accepted());
  CHECK_EQ(s.cpu.regs().pc, 0x0104);
  s.cpu.step();              // MOV:I #1 then interrupt
  CHECK(s.cpu.irq_accepted());
  CHECK_EQ(s.cpu.regs().r[0], 1);
  CHECK_EQ(s.cpu.regs().pc, 0x0500);
  CHECK_EQ(s.peek16(0xEFFE), 0x0107);
}

// The new mask takes effect on the third state after LDC (4.8.1 note): a
// 2-state NOP after it is not enough, the interrupt waits one more instruction.
void test_irq_mask_delay_after_ldc() {
  System s;
  s.poke16(0x0040, 0x0500);
  s.poke(0x0100, {
      0x0C, 0x00, 0x00, 0x88,  // LDC.W #H'0000,SR
      0x00,                    // NOP (2 states)
      0x50, 0x01,              // MOV:E #1,R0  (executes too)
      0x50, 0x02,              // MOV:E #2,R0  (after the IRQ)
  });
  s.poke(0x0500, {0x0A});
  s.start(0x0100, 0xF000);
  s.cpu.set_irq(3, 32);
  s.cpu.step();  // LDC
  s.cpu.step();  // NOP: mask not yet effective
  CHECK(!s.cpu.irq_accepted());
  CHECK_EQ(s.cpu.regs().pc, 0x0105);
  s.cpu.step();  // MOV:E #1, then the interrupt
  CHECK(s.cpu.irq_accepted());
  CHECK_EQ(s.cpu.regs().r[0] & 0xFF, 1);
  CHECK_EQ(s.peek16(0xEFFE), 0x0107);
  // An interrupt arriving much later is unaffected by the stale delay state.
  s.cpu.set_irq(0, 0);
  s.cpu.step();  // RTE
  s.cpu.step();  // MOV:E #2
  s.cpu.set_irq(3, 32);
  s.cpu.step();
  CHECK(s.cpu.irq_accepted());
}

// Byte access through @-SP / @SP+ is a word access: SP moves by 2 and the
// byte lives in the odd lane of the word slot.
void test_stack_byte_odd_lane() {
  System s;
  s.poke(0x0100, {
      0x50, 0x5A,  // MOV:E #H'5A,R0
      0xB7, 0x90,  // MOV.B R0,@-R7
      0xC7, 0x81,  // MOV.B @R7+,R1
  });
  s.start(0x0100, 0x0202);
  s.cpu.step();
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[7], 0x0200);
  CHECK_EQ(s.ram()[0x0201], 0x5A);
  CHECK_EQ(s.ram()[0x0200], 0x00);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[1] & 0xFF, 0x5A);
  CHECK_EQ(s.cpu.regs().r[7], 0x0202);
}

// LDC.B/STC.B through the stack: EP moves the EP:DP pair, other registers
// store their byte in both lanes and load from the odd lane.
void test_ldc_stc_stack_pair() {
  System s;
  s.poke(0x0100, {
      0xB7, 0x9C,  // STC.B EP,@-SP
      0xC7, 0x8C,  // LDC.B @SP+,EP
      0xB7, 0x9D,  // STC.B DP,@-SP
      0xC7, 0x8D,  // LDC.B @SP+,DP
  });
  s.start(0x0100, 0x0400);
  s.cpu.regs().ep = 0x12;
  s.cpu.regs().dp = 0x34;
  s.cpu.step();  // STC.B EP
  CHECK_EQ(s.cpu.regs().r[7], 0x03FE);
  CHECK_EQ(s.peek16(0x03FE), 0x1234);
  s.cpu.regs().ep = 0;
  s.cpu.regs().dp = 0;
  s.cpu.step();  // LDC.B EP
  CHECK_EQ(s.cpu.regs().r[7], 0x0400);
  CHECK_EQ(s.cpu.regs().ep, 0x12);
  CHECK_EQ(s.cpu.regs().dp, 0x34);
  s.cpu.regs().dp = 0x56;
  s.cpu.step();  // STC.B DP: duplicated
  CHECK_EQ(s.peek16(0x03FE), 0x5656);
  s.poke16(0x03FE, 0x1278);
  s.cpu.step();  // LDC.B DP: odd lane, EP untouched
  CHECK_EQ(s.cpu.regs().dp, 0x78);
  CHECK_EQ(s.cpu.regs().ep, 0x12);
}

// Word forms of the byte control registers (used by firmware although the
// manual marks them "not allowed").
void test_control_register_word_forms() {
  System s;
  s.poke(0x0100, {
      0x0C, 0x00, 0x03, 0x8D,  // LDC.W #H'0003,DP   -> DP = 3 (low byte)
      0xA8, 0x9D,              // STC.W DP,R0        -> R0 = H'0303
      0x0C, 0x12, 0x34, 0x8C,  // LDC.W #H'1234,EP   -> EP = H'12, DP = H'34
      0xA9, 0x9C,              // STC.W EP,R1        -> R1 = H'1234
      0x04, 0x07, 0x8D,        // LDC.B #7,DP        -> DP = 7, EP unchanged
  });
  s.start(0x0100);
  CHECK_EQ(s.cpu.step(), 6u);  // LDC.W #xx:16: 6 states
  CHECK_EQ(s.cpu.regs().dp, 3);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[0], 0x0303);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().ep, 0x12);
  CHECK_EQ(s.cpu.regs().dp, 0x34);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[1], 0x1234);
  CHECK_EQ(s.cpu.step(), 4u);  // LDC.B #xx:8: 4 states
  CHECK_EQ(s.cpu.regs().dp, 7);
  CHECK_EQ(s.cpu.regs().ep, 0x12);
}

// ADDX has a sticky Z (previous Z AND result zero); SUBX does not.
void test_addx_subx_zero_flag() {
  System s;
  s.poke(0x0100, {
      0xA9, 0xA0,  // ADDX.W R1,R0
      0xA9, 0xB0,  // SUBX.W R1,R0
  });
  s.start(0x0100);
  s.cpu.regs().r[0] = 0xFFFF;
  s.cpu.regs().r[1] = 0x0001;
  s.cpu.regs().sr &= u16(~(Cpu::kZ | Cpu::kC));  // Z = 0 beforehand
  s.cpu.step();  // 0xFFFF + 1 = 0 with carry, but Z stays 0
  CHECK_EQ(s.cpu.regs().r[0], 0);
  CHECK((s.cpu.regs().sr & Cpu::kZ) == 0);
  CHECK((s.cpu.regs().sr & Cpu::kC) != 0);
  s.cpu.regs().r[0] = 0x0002;
  s.cpu.regs().r[1] = 0x0001;  // 2 - 1 - C(1) = 0 -> Z = 1 although Z was 0
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[0], 0);
  CHECK((s.cpu.regs().sr & Cpu::kZ) != 0);
}

void test_swap_and_bit_number_flags() {
  System s;
  s.poke(0x0100, {
      0xA0, 0x10,  // SWAP R0
      0xA0, 0x10,  // SWAP R0
      0xA1, 0xF9,  // BTST.B #9,R1  -> bit beyond the byte: Z = 1
      0xA1, 0xC9,  // BSET.B #9,R1  -> no effect on the byte
      0xA1, 0xF1,  // BTST.B #1,R1  -> Z = 0
  });
  s.start(0x0100);
  s.cpu.regs().r[0] = 0x0080;
  s.cpu.step();  // -> H'8000: N set from the word result
  CHECK_EQ(s.cpu.regs().r[0], 0x8000);
  CHECK((s.cpu.regs().sr & Cpu::kN) != 0);
  CHECK((s.cpu.regs().sr & Cpu::kZ) == 0);
  s.cpu.step();  // -> H'0080
  CHECK((s.cpu.regs().sr & Cpu::kN) == 0);
  s.cpu.regs().r[1] = 0xFFFF;
  s.cpu.step();
  CHECK((s.cpu.regs().sr & Cpu::kZ) != 0);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[1], 0xFFFF);
  s.cpu.step();
  CHECK((s.cpu.regs().sr & Cpu::kZ) == 0);
}

void test_divxu_zero_divide() {
  System s;
  s.poke16(0x0006, 0x0600);            // zero divide vector
  s.poke(0x0100, {0xA1, 0xB8});        // DIVXU.B R1,R0
  s.start(0x0100, 0xF000);
  s.cpu.regs().r[0] = 0x1234;
  s.cpu.regs().r[1] = 0;
  const unsigned t = s.cpu.step();
  CHECK_EQ(t, 20u);
  CHECK_EQ(s.cpu.regs().pc, 0x0600);
  CHECK((s.cpu.regs().sr & Cpu::kZ) != 0);
  CHECK_EQ(s.peek16(0xEFFE), 0x0102);
}

void test_divxu_and_mulxu() {
  System s;
  s.poke(0x0100, {
      0xA1, 0xB8,  // DIVXU.B R1,R0     0x1234 / 0x10 -> q=0x123 overflow -> V=1, R0 unchanged, 20-12 = 8 states
      0xA1, 0xB8,  // DIVXU.B R1,R0     0x0234 / 0x10 -> q=0x23 r=4 -> R0 = 0x0423
      0xA1, 0xA8,  // MULXU.B R1,R0     0x23 * 0x10 = 0x0230
      0xAB, 0xB8,  // DIVXU.W R3,R0     R0:R1 = 0x0230_0000 / 0x0100 = 0x00023000 overflow
  });
  s.start(0x0100);
  s.cpu.regs().r[0] = 0x1234;
  s.cpu.regs().r[1] = 0x0010;
  CHECK_EQ(s.cpu.step(), 8u);
  CHECK((s.cpu.regs().sr & Cpu::kV) != 0);
  CHECK_EQ(s.cpu.regs().r[0], 0x1234);
  s.cpu.regs().r[0] = 0x0234;
  CHECK_EQ(s.cpu.step(), 20u);
  CHECK_EQ(s.cpu.regs().r[0], 0x0423);
  CHECK((s.cpu.regs().sr & Cpu::kV) == 0);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[0], 0x0230);
  s.cpu.regs().r[1] = 0;
  s.cpu.regs().r[3] = 0x0100;
  CHECK_EQ(s.cpu.step(), 26u - 18u);
  CHECK((s.cpu.regs().sr & Cpu::kV) != 0);
}

void test_max_mode_pjsr() {
  System s(ChipModel::H8_510, 4);
  s.bus.map_ram(0x010000, 0x10000, BusClass::W16_S2);
  s.poke(0x0000, {0x00, 0x00, 0x01, 0x00});          // reset vector: CP=0, PC=H'0100
  s.poke(0x0100, {0x03, 0x01, 0x20, 0x00});          // PJSR @H'012000
  s.poke(0x012000, {0x11, 0x19});                    // PRTS
  s.cpu.reset();
  s.cpu.regs().r[7] = 0xF000;
  s.cpu.regs().tp = 0;
  CHECK(s.cpu.max_mode());
  CHECK_EQ(s.cpu.regs().pc, 0x0100);
  CHECK_EQ(s.cpu.step(), 15u);
  CHECK_EQ(s.cpu.regs().cp, 1);
  CHECK_EQ(s.cpu.regs().pc, 0x2000);
  CHECK_EQ(s.cpu.regs().r[7], 0xEFFC);
  CHECK_EQ(s.peek16(0xEFFE), 0x0104);
  CHECK_EQ(s.cpu.step(), 12u);
  CHECK_EQ(s.cpu.regs().cp, 0);
  CHECK_EQ(s.cpu.regs().pc, 0x0104);
  CHECK_EQ(s.cpu.cache_pages_allocated(), 2u);
}

void test_invalid_instruction_exception() {
  System s;
  s.poke16(0x0004, 0x0700);
  s.poke(0x0100, {0x0B});
  s.start(0x0100, 0xF000);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().pc, 0x0700);
  CHECK_EQ(s.cpu.exceptions_taken(), 1u);
  CHECK_EQ(s.peek16(0xEFFE), 0x0100);  // PC of the invalid instruction
}

void test_address_error() {
  System s;
  s.poke16(0x0010, 0x0800);           // address error vector
  s.poke(0x0100, {0xD8, 0x81, 0x00}); // MOV.W @R0,R1 ; NOP
  s.start(0x0100, 0xF000);
  s.cpu.regs().r[0] = 0x0301;         // odd word address
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().pc, 0x0800);
  CHECK_EQ(s.peek16(0xEFFE), 0x0102); // next instruction
  CHECK_EQ(s.cpu.exceptions_taken(), 1u);

  // Prefetch from the no-execute register field.
  System t;
  t.bus.set_noexec(0xFE80, 0x180);
  t.poke16(0x0010, 0x0800);
  t.poke(0x0100, {0x10, 0xFE, 0x80}); // JMP @H'FE80:16
  t.start(0x0100, 0xF000);
  t.cpu.step();                       // JMP
  CHECK_EQ(t.cpu.regs().pc, 0xFE80);
  t.cpu.step();                       // prefetch -> address error
  CHECK_EQ(t.cpu.regs().pc, 0x0800);
  CHECK_EQ(t.peek16(0xEFFE), 0xFE80);
}

void test_sleep_and_wake() {
  System s;
  s.poke16(0x0040, 0x0500);
  s.poke(0x0100, {0x1A, 0x00});  // SLEEP ; NOP
  s.poke(0x0500, {0x0A});
  s.start(0x0100, 0xF000);
  s.cpu.regs().sr &= u16(~Cpu::kMaskBits);
  CHECK_EQ(s.cpu.step(), 2u);
  CHECK(s.cpu.sleeping());
  CHECK_EQ(s.cpu.step(), Cpu::kSleepIdleStates);
  CHECK_EQ(s.cpu.regs().pc, 0x0101);
  const u64 before = s.cpu.total_states();
  s.cpu.run(1000);
  CHECK_EQ(s.cpu.total_states() - before, 1000u);
  s.cpu.set_irq(2, 32);
  s.cpu.step();
  CHECK(!s.cpu.sleeping());
  CHECK_EQ(s.cpu.regs().pc, 0x0500);
  CHECK_EQ(s.peek16(0xEFFE), 0x0101);
}

// Code in RAM: writes through the bus drop the decoded cells.
void test_self_modifying_code() {
  System s;
  s.poke(0x0100, {
      0x50, 0x01,        // MOV:E #1,R0
      0x20, 0xFC,        // BRA -4 (back to 0x0100)
  });
  s.start(0x0100);
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[0] & 0xFF, 1);
  s.cpu.step();  // BRA
  s.bus.write8(0x0101, 0x02);  // patch the immediate
  s.cpu.step();
  CHECK_EQ(s.cpu.regs().r[0] & 0xFF, 2);
}

// run() stops at instruction boundaries and reports the overshoot.
void test_run_budget() {
  System s;
  s.poke(0x0100, {0xA9, 0x08, 0x20, 0xFC});  // ADD:Q.W #1,R1 (2 states) ; BRA -4 (7 states)
  s.start(0x0100);
  const u64 used = s.cpu.run(10);  // 2 + 7 + 2 = 11
  CHECK_EQ(used, 11u);
  CHECK_EQ(s.cpu.total_states(), 11u);
  CHECK_EQ(s.cpu.instructions_executed(), 3u);
  CHECK_EQ(s.cpu.regs().pc, 0x0102);
}

}  // namespace

int main() {
  test_reset_and_nop();
  test_add_timing_parity();
  test_jsr_timing_slow_stack();
  test_fetch_from_slow_bus();
  test_wait_states();
  test_flags_and_alu();
  test_memory_immediates();
  test_scb_loop();
  test_stack_ops();
  test_trapa_and_rte();
  test_trace();
  test_interrupt();
  test_irq_deferred_after_ldc();
  test_irq_mask_delay_after_ldc();
  test_stack_byte_odd_lane();
  test_ldc_stc_stack_pair();
  test_control_register_word_forms();
  test_addx_subx_zero_flag();
  test_swap_and_bit_number_flags();
  test_divxu_zero_divide();
  test_divxu_and_mulxu();
  test_max_mode_pjsr();
  test_invalid_instruction_exception();
  test_address_error();
  test_sleep_and_wake();
  test_self_modifying_code();
  test_run_budget();
  return test::finish("test_cpu");
}

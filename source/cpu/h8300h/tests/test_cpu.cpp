// CPU execution tests: reset, arithmetic flags, addressing modes, branches,
// bit operations, the stack, exceptions, the interrupt hold-off and the
// access-based timing model.
#include <initializer_list>

#include "cpu/h8300h/disasm.hpp"
#include "cpu/h8300h/machine.hpp"
#include "common/test_util.hpp"

using namespace h8300h;

namespace {

constexpr u32 kCode = 0x001000, kRam = 0x200000;

struct System {
  Machine m;
  System() {
    m.map_rom(0x000000, 0x100000);
    m.map_ram(kRam, 0x40000);
    u8* rom = m.bus().mem();
    vector(0, kCode);
    m.reset();
    m.cpu().set_sp(kRam + 0x1000);
  }
  Cpu& cpu() { return m.cpu(); }
  u8* mem() { return m.bus().mem(); }
  void vector(unsigned n, u32 target) {
    u8* v = mem() + n * 4;
    v[0] = 0; v[1] = u8(target >> 16); v[2] = u8(target >> 8); v[3] = u8(target);
  }
  void code(std::initializer_list<u8> bytes, u32 at = kCode) {
    u32 a = at;
    for (u8 b : bytes) mem()[a++] = b;
    cpu().invalidate_range(at, u32(bytes.size()));
  }
  void steps(unsigned n) { for (unsigned i = 0; i < n; ++i) m.step(); }
  u8 flags() { return cpu().ccr() & 0x2F; }
};

void test_reset() {
  System s;
  CHECK_EQ(s.cpu().pc(), kCode);
  CHECK_EQ(s.cpu().ccr(), kCcrI);
  CHECK_EQ(s.m.periph().sysreg(kSyscr), 0x0B);
  CHECK_EQ(s.m.periph().sysreg(kMdcr), 0xC4);
  CHECK_EQ(s.m.bus().read8(kSyscr), 0x0B);
}

void test_alu_flags() {
  System s;
  s.code({
      0x79, 0x00, 0x7F, 0xFF,  // MOV.W #7FFF,R0
      0x79, 0x10, 0x00, 0x01,  // ADD.W #1,R0        -> 8000, N V
      0xF8, 0x0F,              // MOV.B #0F,R0L
      0x88, 0x01,              // ADD.B #1,R0L       -> 10, H
      0xA8, 0x11,              // CMP.B #11,R0L      -> N C (10 - 11)
      0x7A, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,  // MOV.L #FFFFFFFF,ER1
      0x7A, 0x11, 0x00, 0x00, 0x00, 0x01,  // ADD.L #1,ER1 -> 0, Z C H
      0x0B, 0x71,              // INC.L #1,ER1
      0x1B, 0x71,              // DEC.L #1,ER1 -> 0, Z
  });
  s.steps(2);
  CHECK_EQ(s.cpu().reg16(0), 0x8000);
  CHECK_EQ(s.flags(), kCcrN | kCcrV | kCcrH);  // H: carry out of bit 11
  s.steps(2);
  CHECK_EQ(s.cpu().reg8(8), 0x10);
  CHECK_EQ(s.flags(), kCcrH);
  s.steps(1);
  CHECK_EQ(s.flags(), kCcrN | kCcrC | kCcrH);  // H: borrow at bit 3
  s.steps(2);
  CHECK_EQ(s.cpu().er(1), 0u);
  CHECK_EQ(s.flags(), kCcrZ | kCcrC | kCcrH);
  s.steps(2);
  CHECK_EQ(s.cpu().er(1), 0u);
  CHECK_EQ(s.flags(), kCcrZ | kCcrC | kCcrH);  // INC/DEC leave H and C alone
}

void test_memory_forms() {
  System s;
  s.code({
      0x7A, 0x02, 0x00, 0x20, 0x00, 0x10,  // MOV.L #200010,ER2
      0x79, 0x03, 0x12, 0x34,              // MOV.W #1234,R3
      0x69, 0xA3,                          // MOV.W R3,@ER2
      0x6F, 0xA3, 0x00, 0x04,              // MOV.W R3,@(4,ER2)
      0x6D, 0xA3,                          // MOV.W R3,@-ER2     (200010 -> 20000E)
      0x6D, 0x24,                          // MOV.W @ER2+,R4     -> 1234, ER2 = 200010
      0x6A, 0x84, 0x00, 0x30,              // MOV.B R4L... (MOV.B R4H,@0030:16) -> writes FFFF... no: aa:16 sign-extended
      0x6B, 0x25, 0x00, 0x20, 0x00, 0x14,  // MOV.W @200014:24,R5
      0x01, 0x00, 0x6B, 0xA2, 0x00, 0x20, 0x00, 0x20,  // MOV.L ER2,@200020:24
      0x7D, 0x20, 0x70, 0x00,              // BSET #0,@ER2
      0x7E, 0x30, 0x73, 0x10,              // BTST #1,@30:8   (FFFF30)
  });
  s.steps(3);
  CHECK_EQ(s.m.bus().read16(0x200010), 0x1234);
  s.steps(1);
  CHECK_EQ(s.m.bus().read16(0x200014), 0x1234);
  s.steps(2);
  CHECK_EQ(s.cpu().er(2), 0x200010u);
  CHECK_EQ(s.cpu().reg16(4), 0x1234);
  CHECK_EQ(s.m.bus().read16(0x20000E), 0x1234);
  s.steps(1);  // MOV.B R4H,@0030:16 -> address FFFF0030 & mask = FF0030? aa:16 sign-extends: 0x0030 -> 000030 (ROM: write ignored)
  s.steps(1);
  CHECK_EQ(s.cpu().reg16(5), 0x1234);
  s.steps(1);
  CHECK_EQ(s.m.bus().read32(0x200020), 0x200010u);
  s.steps(1);
  CHECK_EQ(s.m.bus().read8(0x200010), 0x13);
  s.steps(1);
  CHECK(!(s.flags() & kCcrZ));  // unclaimed register-field bytes read all ones
}

void test_branches_and_stack() {
  System s;
  s.code({
      0xF8, 0x01,              // MOV.B #1,R0L
      0x1A, 0x08,              // DEC.B R0L         -> 0, Z
      0x47, 0x02,              // BEQ +2
      0xF8, 0x55,              // (skipped) MOV.B #55,R0L
      0x5E, 0x00, 0x10, 0x20,  // JSR @001020:24
      0xF9, 0x77,              // MOV.B #77,R1L
      0x40, 0xFE,              // BRA -2 (self)
  });
  s.code({
      0xF9, 0x66,  // MOV.B #66,R1L
      0x54, 0x70,  // RTS
  }, 0x001020);
  s.steps(3);
  CHECK_EQ(s.cpu().pc(), kCode + 8);
  s.steps(1);
  CHECK_EQ(s.cpu().pc(), 0x001020u);
  CHECK_EQ(s.cpu().sp(), kRam + 0x1000 - 4);
  CHECK_EQ(s.m.bus().read32(kRam + 0x1000 - 4), kCode + 12);
  s.steps(2);
  CHECK_EQ(s.cpu().pc(), kCode + 12);
  CHECK_EQ(s.cpu().reg8(9), 0x66);
  s.steps(1);
  CHECK_EQ(s.cpu().reg8(9), 0x77);
}

void test_trapa_and_rte() {
  System s;
  u8* rom = s.mem();
  const u32 handler = 0x001100;
  (void)rom;
  s.vector(9, handler);
  s.code({
      0x04, 0x01,  // ORC #1,CCR
      0x57, 0x10,  // TRAPA #1
      0xF8, 0x11,  // MOV.B #11,R0L
  });
  s.code({0xF9, 0x22, 0x56, 0x70}, handler);  // MOV.B #22,R1L; RTE
  s.steps(2);
  CHECK_EQ(s.cpu().pc(), handler);
  CHECK(s.cpu().ccr() & kCcrI);
  CHECK_EQ(s.m.bus().read32(s.cpu().sp()), ((kCcrI | 1u) << 24) | (kCode + 4));
  s.steps(2);
  CHECK_EQ(s.cpu().pc(), kCode + 4);
  CHECK_EQ(s.cpu().ccr(), kCcrI | 1);
  CHECK_EQ(s.cpu().reg8(9), 0x22);
}

void test_mul_div_shift() {
  System s;
  s.code({
      0x79, 0x00, 0x00, 0x64,  // MOV.W #100,R0
      0xF9, 0x07,              // MOV.B #7,R1L
      0x51, 0x90,              // DIVXU.B R1L,R0   -> R0L = 14, R0H = 2
      0x79, 0x02, 0x12, 0x34,  // MOV.W #1234,R2
      0x10, 0x02,              // SHLL.B R2L... (SHLL.B R2H? 10 02 = SHLL.B R2H)
      0x12, 0x0A,              // ROTXL.B R2L
      0x7A, 0x03, 0xFF, 0xFF, 0xFF, 0xF0,  // MOV.L #-16,ER3
      0x11, 0xB3,              // SHAR.L ER3 -> -8
      0x17, 0xF3,              // EXTS.L ER3
  });
  s.steps(3);
  CHECK_EQ(s.cpu().reg8(8), 14);
  CHECK_EQ(s.cpu().reg8(0), 2);
  s.steps(2);
  CHECK_EQ(s.cpu().reg8(2), 0x24);
  CHECK(!(s.cpu().ccr() & kCcrC));
  s.steps(1);
  CHECK_EQ(s.cpu().reg8(10), 0x68);
  s.steps(2);
  CHECK_EQ(s.cpu().er(3), 0xFFFFFFF8u);
  s.steps(1);
  CHECK_EQ(s.cpu().er(3), 0xFFFFFFF8u);
}

void test_timing() {
  System s;
  // ROM in area 0 at the reset image of the bus controller: 16-bit, 3 states
  // plus the 3 programmable waits (WCR = F3, WCER = FF) = 6 per word.
  // Register-field access three states per byte (per word for the 16-bit
  // registers), internal RAM two per word.
  s.code({
      0x00, 0x00,              // NOP: 1 word fetch = 6 states
      0x79, 0x00, 0x12, 0x34,  // MOV.W #1234,R0: 2 words = 12
      0x6B, 0x80, 0xFF, 0xE0,  // MOV.W R0,@FFE0:16 (register field, 16-bit register): 12 + 3 = 15
      0x6B, 0x80, 0xFD, 0x10,  // MOV.W R0,@FD10:16 (on-chip RAM): 12 + 2 = 14
      0x40, 0x00,              // BRA +0: 6 + 6 (fetch at target) = 12
      0x40, 0xFE,              // BRA self: 6 (the target word was just fetched)
  });
  const u64 t0 = s.m.now();
  s.steps(1);
  CHECK_EQ(s.m.now() - t0, 6u);
  s.steps(1);
  CHECK_EQ(s.m.now() - t0, 18u);
  s.steps(1);
  CHECK_EQ(s.m.now() - t0, 33u);
  s.steps(1);
  CHECK_EQ(s.m.now() - t0, 47u);
  s.steps(1);
  CHECK_EQ(s.m.now() - t0, 59u);
  s.steps(1);
  CHECK_EQ(s.m.now() - t0, 65u);
  // Without the waits (WMS = 11) an area-0 word costs 3.
  s.m.bus().write8(kWcr, 0xFF);
  const u64 t1 = s.m.now();
  s.steps(1);
  CHECK_EQ(s.m.now() - t1, 3u);
}

void test_disassembly() {
  System s;
  s.code({0x6B, 0xA0, 0x00, 0x20, 0x00, 0x00, 0x47, 0x02, 0x7C, 0x20, 0x73, 0x00});
  DecodedInsn d = s.cpu().decode_at(kCode);
  CHECK_EQ(disassemble(d), "MOV.W R0,@H'200000:24");
  d = s.cpu().decode_at(kCode + 6);
  CHECK_EQ(disassemble(d, kCode + 8), "BEQ H'00100A:8");
  d = s.cpu().decode_at(kCode + 8);
  CHECK_EQ(disassemble(d), "BTST #0,@ER2");
}

void test_sleep_and_interrupt() {
  System s;
  u8* rom = s.mem();
  const u32 handler = 0x001200;
  (void)rom;
  s.vector(kVecIrq0, handler);
  s.code({
      0x06, 0x7F,              // ANDC #7F,CCR  (I clear; next instruction still runs)
      0x01, 0x80,              // SLEEP
      0xF8, 0x11,              // MOV.B #11,R0L
      0x40, 0xFE,              // BRA self
  });
  s.code({0xF9, 0x22, 0x56, 0x70}, handler);
  s.m.bus().write8(kIer, 0x01);
  s.steps(2);
  CHECK(s.cpu().sleeping());
  const u64 t = s.m.now();
  s.m.run(1000);
  CHECK(s.cpu().sleeping());
  CHECK_EQ(s.m.now() - t, 1000u);
  s.m.periph().request(kVecIrq0);
  s.m.step();  // the entry and the handler's first instruction
  CHECK(!s.cpu().sleeping());
  CHECK_EQ(s.cpu().pc(), handler + 2);
  CHECK_EQ(s.cpu().reg8(9), 0x22);
  s.steps(1);
  CHECK_EQ(s.cpu().pc(), kCode + 4);
  // The hold-off: an interrupt requested during ANDC is taken only after the
  // following instruction.
  s.m.reset();
  s.cpu().set_sp(kRam + 0x1000);
  s.m.bus().write8(kIer, 0x01);
  s.m.periph().request(kVecIrq0);
  s.steps(1);  // ANDC clears I; request pending
  CHECK_EQ(s.cpu().pc(), kCode + 2);
  s.steps(1);  // SLEEP runs first
  CHECK_EQ(s.cpu().pc(), kCode + 4);
  CHECK(s.cpu().sleeping());
  s.steps(1);  // then the interrupt wakes it
  CHECK_EQ(s.cpu().pc(), handler + 2);
}

void test_long_moves() {
  System s;
  s.code({
      0x7A, 0x05, 0x12, 0x34, 0x56, 0x78,  // MOV.L #12345678,ER5
      0x01, 0x00, 0x6D, 0xF5,              // MOV.L ER5,@-ER7   (push)
      0x01, 0x00, 0x6D, 0x73,              // MOV.L @ER7+,ER3   (pop)
      0x01, 0x00, 0x6F, 0xF5, 0x00, 0x14,  // MOV.L ER5,@(14:16,ER7)
      0x01, 0x00, 0x6F, 0x72, 0x00, 0x14,  // MOV.L @(14:16,ER7),ER2
      0x7A, 0x01, 0x00, 0x20, 0x01, 0x00,  // MOV.L #200100,ER1
      0x01, 0x00, 0x69, 0x95,              // MOV.L ER5,@ER1
      0x01, 0x00, 0x69, 0x10,              // MOV.L @ER1,ER0
      0x6D, 0xF4,                          // MOV.W R4,@-ER7
      0x6D, 0x76,                          // MOV.W @ER7+,R6
  });
  const u32 sp = s.cpu().sp();
  s.steps(2);
  CHECK_EQ(s.cpu().sp(), sp - 4);
  CHECK_EQ(s.m.bus().read32(sp - 4), 0x12345678u);
  s.steps(1);
  CHECK_EQ(s.cpu().sp(), sp);
  CHECK_EQ(s.cpu().er(3), 0x12345678u);
  s.steps(2);
  CHECK_EQ(s.m.bus().read32(sp + 0x14), 0x12345678u);
  CHECK_EQ(s.cpu().er(2), 0x12345678u);
  s.steps(3);
  CHECK_EQ(s.m.bus().read32(0x200100), 0x12345678u);
  CHECK_EQ(s.cpu().er(0), 0x12345678u);
  s.cpu().set_reg16(4, 0xBEEF);
  s.steps(2);
  CHECK_EQ(s.cpu().sp(), sp);
  CHECK_EQ(s.cpu().reg16(6), 0xBEEF);
}

void test_bus_retime_and_ram_tail() {
  System s;
  s.code({
      0x00, 0x00,              // NOP: 6 states at the reset timing (3 + 3 waits)
      0xF8, 0xFF,              // MOV.B #FF,R0L
      0x38, 0xEE,              // MOV.B R0L,@EE:8  (WCR = FF: no waits) 6 + 3
      0x00, 0x00,              // NOP: fetched at the new timing: 3
      0x79, 0x01, 0x12, 0x34,  // MOV.W #1234,R1
      0x6B, 0x81, 0xFF, 0x0E,  // MOV.W R1,@FFFF0E:16: the RAM tail in the register field's line, 6 + 2
      0x6B, 0x02, 0xFF, 0x0E,  // MOV.W @FFFF0E:16,R2
  });
  const u64 t0 = s.m.now();
  s.steps(1);
  CHECK_EQ(s.m.now() - t0, 6u);
  s.steps(2);
  CHECK_EQ(s.m.now() - t0, 6u + 6 + 9);
  CHECK_EQ(s.cpu().pc(), kCode + 6);
  s.steps(1);
  CHECK_EQ(s.m.now() - t0, 6u + 6 + 9 + 3);
  s.steps(2);
  CHECK_EQ(s.m.now() - t0, 6u + 6 + 9 + 3 + 6 + 8);
  CHECK_EQ(s.m.bus().mem()[0xFFFF0E], 0x12);
  CHECK_EQ(s.m.bus().mem()[0xFFFF0F], 0x34);
  s.steps(1);
  CHECK_EQ(s.cpu().reg16(2), 0x1234);
}

}  // namespace

int main() {
  test_long_moves();
  test_bus_retime_and_ram_tail();
  test_reset();
  test_alu_flags();
  test_memory_forms();
  test_branches_and_stack();
  test_trapa_and_rte();
  test_mul_div_shift();
  test_timing();
  test_disassembly();
  test_sleep_and_interrupt();
  return test::finish("h8300h_test_cpu");
}

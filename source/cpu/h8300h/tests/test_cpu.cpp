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

// --- H8S/2000 ---------------------------------------------------------------------------------
// A bare bus and an H8S core with one interrupt source behind a controller in
// interrupt control mode 2.
struct H8s {
  struct Intc final : IntcBlock {
    bool pending = false;
    u8 lvl = 5;
    bool arbitrate(unsigned& which, u8 exr) const override {
      if (!pending || lvl <= (exr & kExrMask)) return false;  // mode 2: EXR's mask, CCR.I plays no part
      which = kVector;
      return true;
    }
    void acknowledge(unsigned) override { pending = false; }
    bool standby_wake(u8) const override { return false; }
    u8 syscr() const override { return 0x21; }
    void enter_software_standby() override {}
    unsigned control_mode() const override { return 2; }
    u8 level(unsigned) const override { return lvl; }
  };
  static constexpr unsigned kVector = 64;
  Bus bus;
  Cpu cpu{bus, true};
  Intc intc;
  H8s() {
    bus.map_ram(0, 0x400000);
    u8* v = bus.mem();
    v[0] = 0; v[1] = u8(kCode >> 16); v[2] = u8(kCode >> 8); v[3] = u8(kCode);
    cpu.set_intc(&intc);
    cpu.reset();
    cpu.set_sp(kRam + 0x1000);
  }
  void code(std::initializer_list<u8> bytes, u32 at = kCode) {
    u32 a = at;
    for (u8 b : bytes) bus.mem()[a++] = b;
    cpu.invalidate_range(at, u32(bytes.size()));
  }
  void steps(unsigned n) { for (unsigned i = 0; i < n; ++i) cpu.step(); }
  u8 flags() const { return cpu.ccr() & 0x2F; }
};

void test_h8s_shifts() {
  H8s s;
  s.code({
      0xF8, 0x81,  // MOV.B #81,R0L
      0x10, 0x48,  // SHLL.B #2,R0L     -> 04, C from the second step: 0
      0x79, 0x01, 0x40, 0x00,  // MOV.W #4000,R1
      0x10, 0xD1,  // SHAL.W #2,R1      -> 0000, Z C, V: the sign changed in the first step
      0x7A, 0x02, 0x80, 0x00, 0x00, 0x03,  // MOV.L #80000003,ER2
      0x11, 0xF2,  // SHAR.L #2,ER2     -> E0000000, N C
      0xFB, 0x40,  // MOV.B #40,R3L
      0x12, 0x4B,  // ROTXL.B #2,R3L    C in = 1: 81 (C 0), then 02 (C 1)
  });
  s.steps(2);
  CHECK_EQ(s.cpu.reg8(8), 0x04);
  CHECK_EQ(s.flags(), 0);
  s.steps(2);
  CHECK_EQ(s.cpu.reg16(1), 0);
  CHECK_EQ(s.flags(), kCcrZ | kCcrC | kCcrV);
  s.steps(2);
  CHECK_EQ(s.cpu.er(2), 0xE0000000u);
  CHECK_EQ(s.flags(), kCcrN | kCcrC);
  s.steps(2);
  CHECK_EQ(s.cpu.reg8(11), 0x02);
  CHECK_EQ(s.flags(), kCcrC);
  // without `h8s` the two-bit forms are undefined
  CHECK(!decode([](unsigned i) { return u8(i == 0 ? 0x10 : 0x48); }).valid());
  CHECK(decode([](unsigned i) { return u8(i == 0 ? 0x10 : 0x48); }, true).valid());
}

void test_h8s_ldm_stm_tas() {
  H8s s;
  const u32 sp = s.cpu.sp();
  s.cpu.set_er(0, 0x11111111);
  s.cpu.set_er(1, 0x22222222);
  s.cpu.set_er(6, kRam + 0x200);
  s.bus.mem()[kRam + 0x200] = 0x05;
  s.code({
      0x01, 0x10, 0x6D, 0xF0,  // STM.L (ER0-ER1),@-SP
      0x01, 0x10, 0x6D, 0x75,  // LDM.L @SP+,(ER4-ER5)
      0x01, 0xE0, 0x7B, 0x6C,  // TAS @ER6
  });
  s.steps(1);
  CHECK_EQ(s.cpu.sp(), sp - 8);
  CHECK_EQ(s.bus.read32(sp - 4), 0x11111111u);  // the first register goes first, to the higher address
  CHECK_EQ(s.bus.read32(sp - 8), 0x22222222u);
  s.steps(1);
  CHECK_EQ(s.cpu.sp(), sp);
  CHECK_EQ(s.cpu.er(4), 0x11111111u);
  CHECK_EQ(s.cpu.er(5), 0x22222222u);
  s.cpu.set_ccr(kCcrI | kCcrN | kCcrV | kCcrZ);
  s.steps(1);
  CHECK_EQ(s.bus.mem()[kRam + 0x200], 0x85);
  CHECK_EQ(s.flags(), 0);  // tested before bit 7 was set
}

void test_h8s_exr() {
  H8s s;
  CHECK_EQ(s.cpu.exr(), 0x7F);
  s.code({
      0x01, 0x41, 0x07, 0x02,  // LDC #02,EXR
      0x02, 0x10,              // STC EXR,R0H
      0x01, 0x41, 0x04, 0x80,  // ORC #80,EXR
      0x01, 0x41, 0x06, 0x7F,  // ANDC #7F,EXR
      0x01, 0x41, 0x05, 0x01,  // XORC #01,EXR
      0x01, 0x41, 0x6D, 0xF0,  // STC.W EXR,@-SP
      0x01, 0x41, 0x07, 0x00,  // LDC #00,EXR
      0x01, 0x41, 0x6D, 0x70,  // LDC.W @SP+,EXR
      0x03, 0x10,              // LDC R0H,EXR
  });
  s.steps(1);
  CHECK_EQ(s.cpu.exr(), 0x7A);  // bits 6-3 read 1
  s.steps(1);
  CHECK_EQ(s.cpu.reg8(0), 0x7A);
  s.steps(1);
  CHECK_EQ(s.cpu.exr(), 0xFA);
  s.steps(1);
  CHECK_EQ(s.cpu.exr(), 0x7A);
  s.steps(1);
  CHECK_EQ(s.cpu.exr(), 0x7B);
  s.steps(3);
  CHECK_EQ(s.cpu.exr(), 0x7B);
  s.steps(1);
  CHECK_EQ(s.cpu.exr(), 0x7A);
}

void test_h8s_mode2_interrupt() {
  H8s s;
  const u32 handler = 0x001200, sp = s.cpu.sp();
  u8* v = s.bus.mem() + H8s::kVector * 4;
  v[0] = 0; v[1] = u8(handler >> 16); v[2] = u8(handler >> 8); v[3] = u8(handler);
  s.code({
      0x01, 0x41, 0x07, 0x05,  // LDC #05,EXR    (a level 5 request is masked)
      0x00, 0x00,              // NOP
      0x01, 0x41, 0x07, 0x04,  // LDC #04,EXR    (now it is not; the next instruction still runs)
      0x00, 0x00,              // NOP
      0x00, 0x00,              // NOP
  });
  s.code({0x00, 0x00, 0x56, 0x70}, handler);  // NOP, RTE
  s.intc.pending = true;
  CHECK_EQ(s.cpu.ccr() & kCcrI, kCcrI);  // CCR.I is set from reset and must not mask in mode 2
  s.steps(1);
  CHECK_EQ(s.cpu.pc(), kCode + 4);
  s.steps(2);
  CHECK_EQ(s.cpu.pc(), kCode + 10);
  s.steps(1);
  CHECK_EQ(s.cpu.pc(), kCode + 12);
  s.steps(1);  // the entry and the handler's NOP
  CHECK_EQ(s.cpu.pc(), handler + 2);
  CHECK_EQ(s.cpu.sp(), sp - 6);
  CHECK_EQ(s.bus.read16(sp - 6), 0x7C00);              // EXR, as a word, at the lowest address
  CHECK_EQ(s.bus.read32(sp - 4) & kAddrMask, kCode + 12);
  CHECK_EQ(s.cpu.exr(), 0x7D);                          // the mask raised to the accepted level
  s.steps(1);  // RTE
  CHECK_EQ(s.cpu.pc(), kCode + 12);
  CHECK_EQ(s.cpu.sp(), sp);
  CHECK_EQ(s.cpu.exr(), 0x7C);
  // TRAPA in mode 2 pushes EXR too but leaves the mask alone
  v = s.bus.mem() + 9 * 4;
  v[0] = 0; v[1] = u8(handler >> 16); v[2] = u8(handler >> 8); v[3] = u8(handler);
  s.code({0x57, 0x10}, kCode + 12);  // TRAPA #1
  s.steps(1);
  CHECK_EQ(s.cpu.pc(), handler);
  CHECK_EQ(s.cpu.sp(), sp - 6);
  CHECK_EQ(s.bus.read16(sp - 6), 0x7C00);
  CHECK_EQ(s.cpu.exr(), 0x7C);
}

// Bit stores to memory: BST / BIST to an absolute address take the bit number, not the number as a mask, and a bit
// number from a register uses only its low three bits (firmware passes whole LED numbers there).
void test_h8s_bit_stores() {
  H8s s;
  s.bus.map_ram(0xFFFF00, 0x100);
  u8* m = s.bus.mem();
  m[0xFFFF40] = 0x00; m[0xFFFF41] = 0xFF; m[0xFFFF42] = 0x00; m[0xFFFF43] = 0xFF;
  s.code({
      0x04, 0x01,                          // ORC #1,CCR
      0x7F, 0x40, 0x67, 0x20,              // BST #2,@H'FFFF40
      0x7F, 0x41, 0x67, 0xA0,              // BIST #2,@H'FFFF41
      0xF8, 0x09,                          // MOV.B #9,R0L
      0x7A, 0x01, 0x00, 0xFF, 0xFF, 0x42,  // MOV.L #H'FFFF42,ER1
      0x7D, 0x10, 0x60, 0x80,              // BSET R0L,@ER1
      0x7F, 0x43, 0x62, 0x80,              // BCLR R0L,@H'FFFF43
  });
  s.steps(7);
  CHECK_EQ(m[0xFFFF40], 0x04);
  CHECK_EQ(m[0xFFFF41], 0xFB);
  CHECK_EQ(m[0xFFFF42], 0x02);  // bit 9 is bit 1
  CHECK_EQ(m[0xFFFF43], 0xFD);
}

void test_h8s_disassembly() {
  H8s s;
  s.code({0x01, 0x10, 0x6D, 0x75, 0x01, 0x30, 0x6D, 0xF0, 0x01, 0xE0, 0x7B, 0x6C, 0x10, 0x48, 0x01, 0x41,
          0x07, 0x02, 0x02, 0x10, 0x01, 0x41, 0x6D, 0xF0});
  CHECK_EQ(disassemble(s.cpu.decode_at(kCode)), "LDM.L @SP+,(ER4-ER5)");
  CHECK_EQ(disassemble(s.cpu.decode_at(kCode + 4)), "STM.L (ER0-ER3),@-SP");
  CHECK_EQ(disassemble(s.cpu.decode_at(kCode + 8)), "TAS @ER6");
  CHECK_EQ(disassemble(s.cpu.decode_at(kCode + 12)), "SHLL.B #2,R0L");
  CHECK_EQ(disassemble(s.cpu.decode_at(kCode + 14)), "LDC #H'02,EXR");
  CHECK_EQ(disassemble(s.cpu.decode_at(kCode + 18)), "STC EXR,R0H");
  CHECK_EQ(disassemble(s.cpu.decode_at(kCode + 20)), "STC EXR,@-ER7");
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
  test_h8s_shifts();
  test_h8s_ldm_stm_tas();
  test_h8s_exr();
  test_h8s_mode2_interrupt();
  test_h8s_disassembly();
  test_h8s_bit_stores();
  return test::finish("h8300h_test_cpu");
}

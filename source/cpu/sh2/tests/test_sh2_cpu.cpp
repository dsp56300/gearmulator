// SH-2 core: instruction semantics, delay slots, exceptions, timing.
#include <initializer_list>
#include <vector>

#include "cpu/sh2/disasm.hpp"
#include "cpu/sh2/machine.hpp"
#include "common/test_util.hpp"

using namespace sh2;

namespace {

// SH7014 in MCU mode 1 with 64 KB of 16-bit zero-wait SRAM in CS0 (vectors
// and data) and code placed in on-chip RAM (32-bit, 1 state) unless a test
// wants external fetch timing.
struct System {
  Machine m;
  static constexpr u32 kRam = 0xFFFFF000u;  // on-chip RAM
  System() : m(ChipModel::SH7014, 1) {
    m.bus().map_ram(0x00000000, 0x10000, Bus::kClsCs0);
    m.bus().set_class(Bus::kClsCs0, {16, 1, 0, true, true});
  }
  Bus& bus() { return m.bus(); }
  Cpu& cpu() { return m.cpu(); }
  Cpu::Regs& r() { return m.cpu().regs(); }
  void code(u32 at, std::initializer_list<u16> words) {
    for (u16 w : words) {
      Bus::put_be16(bus().ptr(at), w);
      at += 2;
    }
  }
  void start(u32 pc, u32 sp = 0xFFFFFBF0u) {  // top of the 3 KB on-chip RAM
    bus().write32(0, pc);
    bus().write32(4, sp);
    cpu().invalidate_all();
    m.reset();
    m.bus().write16(0xFFFF8624, 0x0000);  // WCR1: zero wait states (reset gives 15 to every area)
    r().sr &= ~Cpu::kIMask;  // let interrupts through by default
  }
  u64 run_until(u32 pc, u64 max_states) {
    u64 used = 0;
    while (r().pc != pc && used < max_states) used += cpu().step();
    return used;
  }
};

void test_reset_and_basic_moves() {
  System s;
  s.code(System::kRam, {
      0xE07F,          // MOV #H'7F,R0
      0xE1FF,          // MOV #-1,R1
      0x6013,          // MOV R1,R0            R0 = -1
      0xE205,          // MOV #5,R2
      0x9204,          // MOV.W @(8,PC),R2     -> data at +4+8
      0xD301,          // MOV.L @(4,PC),R3     -> (pc+4)&~3 + 4
      0x0009, 0x0009,  // NOP NOP
      0x1234, 0x5678,  // .DATA.L H'12345678  (also .DATA.W)
      0xABCD, 0xEF01,
  });
  s.start(System::kRam);
  CHECK_EQ(s.r().pc, System::kRam);
  CHECK_EQ(s.r().r[15], 0xFFFFFBF0u);
  CHECK_EQ(s.cpu().interrupt_mask(), 0u);
  CHECK_EQ(s.cpu().step(), 1u);
  CHECK_EQ(s.r().r[0], 0x7Fu);
  s.cpu().step();
  CHECK_EQ(s.r().r[1], 0xFFFFFFFFu);
  s.cpu().step();
  CHECK_EQ(s.r().r[0], 0xFFFFFFFFu);
  s.cpu().step();
  s.cpu().step();  // MOV.W @(8,PC): address = (kRam+8) + 4 + 8 = kRam+20 -> 0xABCD sign-extended
  CHECK_EQ(s.r().r[2], 0xFFFFABCDu);
  s.cpu().step();  // MOV.L @(4,PC): ((kRam+10)+4)&~3 + 4 = kRam+16 -> 0x12345678
  CHECK_EQ(s.r().r[3], 0x12345678u);
}

void test_memory_addressing() {
  System s;
  s.code(System::kRam, {
      0xE480,          // MOV #-128,R4
      0x2540,          // MOV.B R4,@R5
      0x6650,          // MOV.W @R5,R6          (byte written then word read: H'80xx)
      0x2452,          // MOV.L R5,@R4?         (R4 = -128: address error) -- skipped below
  });
  // Re-write a safer program: test store/load forms.
  s.code(System::kRam, {
      0xE480,  // MOV #-128,R4
      0x2540,  // MOV.B R4,@R5
      0x6650,  // MOV.B @R5,R6      (0x6650 = MOV.B @R5,R6)
      0x2541,  // MOV.W R4,@R5
      0x6651,  // MOV.W @R5,R6
      0x2542,  // MOV.L R4,@R5
      0x6652,  // MOV.L @R5,R6
      0x2546,  // MOV.L R4,@-R5
      0x6756,  // MOV.L @R5+,R7
      0x1542,  // MOV.L R4,@(8,R5)
      0x5852,  // MOV.L @(8,R5),R8
      0x0546,  // MOV.L R4,@(R0,R5)
      0x095E,  // MOV.L @(R0,R5),R9
      0x8032,  // MOV.B R0,@(2,R3)   R3 = R5
      0x8432,  // MOV.B @(2,R3),R0
      0xC004,  // MOV.B R0,@(4,GBR)
      0xC601,  // MOV.L @(4,GBR),R0
      0x6A83,  // MOV R8,R10
      0x6A88,  // SWAP.B R8,R10
      0x6B89,  // SWAP.W R8,R11
      0x28CD,  // XTRCT R12,R8
  });
  s.start(System::kRam);
  s.r().r[5] = 0x1000;
  s.r().r[3] = 0x1000;
  s.r().r[0] = 0x10;
  s.r().gbr = 0x2000;
  s.r().r[12] = 0x11112222;
  s.cpu().step();
  s.cpu().step();
  CHECK_EQ(s.bus().read8(0x1000), 0x80);
  s.cpu().step();
  CHECK_EQ(s.r().r[6], 0xFFFFFF80u);  // sign-extended byte
  s.cpu().step();
  s.cpu().step();
  CHECK_EQ(s.r().r[6], 0xFFFFFF80u);
  s.cpu().step();
  s.cpu().step();
  CHECK_EQ(s.r().r[6], 0xFFFFFF80u);
  s.cpu().step();  // MOV.L R4,@-R5 -> R5 = 0xFFC, mem[0xFFC] = R4
  CHECK_EQ(s.r().r[5], 0xFFCu);
  CHECK_EQ(s.bus().read32(0xFFC), 0xFFFFFF80u);
  s.cpu().step();  // MOV.L @R5+,R7
  CHECK_EQ(s.r().r[7], 0xFFFFFF80u);
  CHECK_EQ(s.r().r[5], 0x1000u);
  s.cpu().step();
  s.cpu().step();
  CHECK_EQ(s.r().r[8], 0xFFFFFF80u);
  CHECK_EQ(s.bus().read32(0x1008), 0xFFFFFF80u);
  s.cpu().step();
  s.cpu().step();
  CHECK_EQ(s.r().r[9], 0xFFFFFF80u);
  CHECK_EQ(s.bus().read32(0x1010), 0xFFFFFF80u);
  s.r().r[0] = 0x5A;
  s.cpu().step();
  s.r().r[0] = 0;
  s.cpu().step();
  CHECK_EQ(s.r().r[0], 0x5Au);
  CHECK_EQ(s.bus().read8(0x1002), 0x5A);
  s.cpu().step();  // MOV.B R0,@(4,GBR)
  CHECK_EQ(s.bus().read8(0x2004), 0x5A);
  s.cpu().step();  // MOV.L @(4,GBR),R0 -> bytes 2004..2007 = 5A ?? ?? ??
  CHECK_EQ(s.r().r[0] >> 24, 0x5Au);
  s.cpu().step();
  s.cpu().step();  // SWAP.B: FFFFFF80 -> FFFF80FF
  CHECK_EQ(s.r().r[10], 0xFFFF80FFu);
  s.cpu().step();  // SWAP.W: FF80FFFF
  CHECK_EQ(s.r().r[11], 0xFF80FFFFu);
  s.cpu().step();  // XTRCT R12,R8: (R12 << 16) | (R8 >> 16) = 2222FFFF
  CHECK_EQ(s.r().r[8], 0x2222FFFFu);
}

void test_alu_flags() {
  System s;
  s.code(System::kRam, {
      0xE07F, 0x4000, 0x4008, 0x4018, 0x4028,  // MOV #7F,R0; SHLL; SHLL2; SHLL8; SHLL16 -> 7F<<27
      0xE101,                                  // MOV #1,R1
      0x301E,                                  // ADDC R1,R0
      0x301F,                                  // ADDV R1,R0
      0xE2FF, 0x6323,                          // MOV #-1,R2 ; MOV R2,R3
      0x323E,                                  // ADDC R3,R2   (-1 + -1 + T)
      0xE400, 0xE501,                          // MOV #0,R4 ; MOV #1,R5
      0x345A,                                  // SUBC R5,R4   -> 0 - 1 - T
      0xE680,                                  // MOV #-128,R6
      0x4600, 0x4600, 0x4600, 0x4600, 0x4600, 0x4600, 0x4600, 0x4600,  // 8x SHLL: -> 0x80000000? no: -128<<8
      0xE701,                                  // MOV #1,R7
      0x367B,                                  // SUBV R7,R6
      0xE801, 0x4810,                          // MOV #1,R8 ; DT R8 -> 0, T=1
      0x4810,                                  // DT R8 -> -1, T=0
      0xE90A, 0xEA0A, 0x39A0,                  // CMP/EQ R10,R9 -> T=1
      0x39A6,                                  // CMP/HI R10,R9 -> 0
      0x39A3,                                  // CMP/GE -> 1
      0x4915, 0x4A11,                          // CMP/PL R9 -> 1 ; CMP/PZ R10 -> 1
      0x2BCC,                                  // CMP/STR R12,R11: R11=0x11223344 R12=0x00220000 -> byte 2 equal -> 1
      0x6D9B,                                  // NEG R9,R13 -> -10
      0x6E9E,                                  // EXTS.B R9,R14 (10)
      0xEF80, 0x6EFC,                          // MOV #-128,R15?? careful: R15 is SP; use R14: MOV #-128,R15 -> avoid
  });
  s.start(System::kRam);
  s.r().r[11] = 0x11223344;
  s.r().r[12] = 0x00220000;
  for (int i = 0; i < 5; ++i) s.cpu().step();
  CHECK_EQ(s.r().r[0], 0x7Fu << 27);
  CHECK((s.r().sr & Cpu::kT) == 0);  // SHLL16 leaves T from SHLL8 = bit 31 of (7F<<11) = 0; last SHLL set T from MSB
  s.cpu().step();
  s.cpu().step();  // ADDC: 0xF8000000 + 1 + T(0)
  CHECK_EQ(s.r().r[0], 0xF8000001u);
  CHECK((s.r().sr & Cpu::kT) == 0);
  s.cpu().step();  // ADDV: negative + 1 no overflow
  CHECK((s.r().sr & Cpu::kT) == 0);
  s.cpu().step();
  s.cpu().step();
  s.cpu().step();  // ADDC R3,R2: -1 + -1 = FFFFFFFE, carry -> T=1
  CHECK_EQ(s.r().r[2], 0xFFFFFFFEu);
  CHECK((s.r().sr & Cpu::kT) != 0);
  s.cpu().step();
  s.cpu().step();
  s.cpu().step();  // SUBC: 0 - 1 - 1 = FFFFFFFE, borrow -> T=1
  CHECK_EQ(s.r().r[4], 0xFFFFFFFEu);
  CHECK((s.r().sr & Cpu::kT) != 0);
  s.cpu().step();
  for (int i = 0; i < 8; ++i) s.cpu().step();  // -128 << 8 = 0xFFFF8000
  CHECK_EQ(s.r().r[6], 0xFFFF8000u);
  s.cpu().step();
  s.cpu().step();  // SUBV 1: no overflow
  CHECK((s.r().sr & Cpu::kT) == 0);
  CHECK_EQ(s.r().r[6], 0xFFFF7FFFu);
  s.cpu().step();
  s.cpu().step();  // DT -> 0, T=1
  CHECK_EQ(s.r().r[8], 0u);
  CHECK((s.r().sr & Cpu::kT) != 0);
  s.cpu().step();  // DT -> -1, T=0
  CHECK((s.r().sr & Cpu::kT) == 0);
  s.cpu().step(); s.cpu().step(); s.cpu().step();
  CHECK((s.r().sr & Cpu::kT) != 0);   // EQ
  s.cpu().step();
  CHECK((s.r().sr & Cpu::kT) == 0);   // HI
  s.cpu().step();
  CHECK((s.r().sr & Cpu::kT) != 0);   // GE
  s.cpu().step();
  CHECK((s.r().sr & Cpu::kT) != 0);   // PL
  s.cpu().step();
  CHECK((s.r().sr & Cpu::kT) != 0);   // PZ
  s.cpu().step();
  CHECK((s.r().sr & Cpu::kT) != 0);   // STR
  s.cpu().step();
  CHECK_EQ(s.r().r[13], u32(-10));
  s.cpu().step();
  CHECK_EQ(s.r().r[14], 10u);
}

// 64/32 unsigned division, the manual's sequence: R1:R2 / R0 -> quotient in R2.
//   DIV0U ; 32 x { ROTCL R2 ; DIV1 R0,R1 } ; ROTCL R2
void test_division_and_multiply() {
  System s;
  std::vector<u16> prog = {0x0019};           // DIV0U
  for (int i = 0; i < 32; ++i) { prog.push_back(0x4224); prog.push_back(0x3104); }  // ROTCL R2 ; DIV1 R0,R1
  prog.push_back(0x4224);                     // ROTCL R2
  prog.push_back(0x6123);                     // MOV R2,R1
  prog.push_back(0x0217);                     // MUL.L R1,R2
  prog.push_back(0x031A);                     // STS MACL,R3
  prog.push_back(0x245F);                     // MULS.W R5,R4
  prog.push_back(0x061A);                     // STS MACL,R6
  prog.push_back(0x378D);                     // DMULS.L R8,R7
  prog.push_back(0x090A);                     // STS MACH,R9
  prog.push_back(0x0A1A);                     // STS MACL,R10
  u32 at = System::kRam;
  for (u16 w : prog) { Bus::put_be16(s.bus().ptr(at), w); at += 2; }
  s.start(System::kRam);
  s.r().r[0] = 7;
  s.r().r[1] = 0;
  s.r().r[2] = 100;
  s.r().r[4] = u32(-3);
  s.r().r[5] = 1000;
  s.r().r[7] = u32(-2);
  s.r().r[8] = 0x40000000;
  for (int i = 0; i < 66; ++i) s.cpu().step();
  CHECK_EQ(s.r().r[2], 14u);  // 100 / 7
  s.cpu().step();
  s.r().r[2] = 3;
  s.cpu().step(); s.cpu().step();
  CHECK_EQ(s.r().r[3], 42u);
  s.cpu().step(); s.cpu().step();
  CHECK_EQ(s.r().r[6], u32(-3000));
  s.cpu().step(); s.cpu().step(); s.cpu().step();
  CHECK_EQ(s.r().r[9], 0xFFFFFFFFu);  // -2 * 2^30 = -2^31 -> MACH = -1, MACL = 0x80000000
  CHECK_EQ(s.r().r[10], 0x80000000u);
}

void test_mac_and_saturation() {
  System s;
  s.code(System::kRam, {
      0x0028,          // CLRMAC
      0x415F,          // MAC.W @R5+,@R1+
      0x415F,          // MAC.W @R5+,@R1+
      0x001A, 0x000A,  // STS MACL,R0 ; STS MACH,R0? (0x000A = STS MACH,R0) -> use R2: 0x020A
  });
  s.code(System::kRam + 6, {0x001A, 0x020A});
  s.bus().write16(0x3000, 0x7FFF);
  s.bus().write16(0x3002, 0x7FFF);
  s.bus().write16(0x3100, 0x7FFF);
  s.bus().write16(0x3102, 0x7FFF);
  s.start(System::kRam);
  s.r().r[1] = 0x3000;
  s.r().r[5] = 0x3100;
  s.r().sr |= Cpu::kS;  // saturation
  for (int i = 0; i < 5; ++i) s.cpu().step();
  // 2 * 0x7FFF*0x7FFF = 0x7FFE0002 fits: no saturation.
  CHECK_EQ(s.r().r[0], 0x7FFE0002u);
  CHECK_EQ(s.r().r[2], 0u);
  CHECK_EQ(s.r().r[1], 0x3004u);
  // Push it over: MACL near max, add again -> saturates to 7FFFFFFF, MACH bit 0 set.
  s.r().macl = 0x7FFFFFF0;
  s.r().r[1] = 0x3000;
  s.r().r[5] = 0x3100;
  s.r().pc = System::kRam + 2;
  s.cpu().step();
  CHECK_EQ(s.r().macl, 0x7FFFFFFFu);
  CHECK_EQ(s.r().mach & 1, 1u);
  // MAC.L with S = 1: 48-bit saturation as the manual's pseudo-code writes
  // it, MACH = H'00008000 / MACL = 0 on negative overflow.
  s.code(System::kRam + 0x20, {0x015F});  // MAC.L @R5+,@R1+
  s.bus().write32(0x3000, 0x80000000);    // -2^31
  s.bus().write32(0x3100, 0x7FFFFFFF);
  s.r().pc = System::kRam + 0x20;
  s.r().r[1] = 0x3000;
  s.r().r[5] = 0x3100;
  s.r().mach = 0xFFFF8000;  // -2^47
  s.r().macl = 0;
  s.cpu().step();
  CHECK_EQ(s.r().mach, 0x00008000u);
  CHECK_EQ(s.r().macl, 0u);
  // MOV.L Rn,@-Rn stores the value before the decrement.
  s.code(System::kRam + 0x30, {0x2126});  // MOV.L R2,@-R1 with R2 = R1? use MOV.L R1,@-R1: 0x2116
  s.code(System::kRam + 0x30, {0x2116});
  s.r().pc = System::kRam + 0x30;
  s.r().r[1] = 0x3010;
  s.cpu().step();
  CHECK_EQ(s.r().r[1], 0x300Cu);
  CHECK_EQ(s.bus().read32(0x300C), 0x3010u);
}

// Delayed branches: the slot executes before the jump; BSR/RTS; JSR; BF/S loop.
void test_delayed_branches() {
  System s;
  s.code(System::kRam, {
      0xE000,          // 000 MOV #0,R0
      0xA002,          // 002 BRA +4 -> target = 002+4+4 = 00A
      0x7001,          // 004 ADD #1,R0        (delay slot: executes)
      0x7010,          // 006 ADD #16,R0       (skipped)
      0x7010,          // 008 ADD #16,R0       (skipped)
      0xB003,          // 00A BSR +6 -> 00A+4+6 = 014 ; PR = 00E
      0x7002,          // 00C ADD #2,R0        (slot)
      0xE10A,          // 00E MOV #10,R1       (return lands here)
      0x4110,          // 010 L: DT R1
      0x8FFE,          // 012 BF/S L  (-4: 012+4-4 = 012? need 010: disp = -6 -> 0x8FFD)
      0x7004,          // 014 ADD #4,R0 (subroutine) ; also slot of BF/S
      0x000B,          // 016 RTS
      0x7008,          // 018 ADD #8,R0        (slot of RTS)
      0x0009,          // 01A NOP
  });
  // Fix the BF/S displacement: target 010 from 012: disp = (010 - 016)/2 = -3 -> 0xFD
  s.code(System::kRam + 0x12, {0x8FFD});
  s.start(System::kRam);
  // Run: MOV; BRA(+slot ADD 1) -> 00A: BSR(+slot ADD 2) -> 014: ADD 4; RTS (+slot ADD 8) -> 00E
  s.cpu().step();               // MOV
  CHECK_EQ(s.cpu().step(), 3u); // BRA (2) + slot (1)
  CHECK_EQ(s.r().r[0], 1u);
  CHECK_EQ(s.r().pc, System::kRam + 0x0A);
  s.cpu().step();               // BSR + slot
  CHECK_EQ(s.r().r[0], 3u);
  CHECK_EQ(s.r().pr, System::kRam + 0x0E);
  CHECK_EQ(s.r().pc, System::kRam + 0x14);
  s.cpu().step();               // ADD #4
  s.cpu().step();               // RTS + slot ADD 8
  CHECK_EQ(s.r().r[0], 15u);
  CHECK_EQ(s.r().pc, System::kRam + 0x0E);
  // Loop: MOV #10,R1; L: DT R1; BF/S L with slot ADD #4,R0: 10 iterations.
  const u32 r0 = s.r().r[0];
  s.run_until(System::kRam + 0x16, 1000);
  CHECK_EQ(s.r().r[1], 0u);
  CHECK_EQ(s.r().r[0], r0 + 10 * 4);  // slot executes on the 9 taken branches + the final not-taken falls into 014
}

// Register field of the 0100 group and BRAF/BSRF: bits 11-8.
void test_register_fields() {
  System s;
  s.code(System::kRam, {
      0xD304,          // 000 MOV.L @(16,PC),R3   -> kRam+0x14: 0xFFFFF010 (subroutine)
      0x430B,          // 002 JSR @R3
      0xE105,          // 004 MOV #5,R1           (slot)
      0x4E1E,          // 006 LDC R14,GBR
      0x4D2A,          // 008 LDS R13,PR
      0x0223,          // 00A BRAF R2             (R2 = 4 -> 00A+4+4 = 012)
      0x0009,          // 00C NOP (slot)
      0xE7FF,          // 00E MOV #-1,R7          (skipped)
      0xFFFF,          // 010 pad
      0xE709,          // 012 MOV #9,R7
      0xFFFF, 0xF01A,  // 014 .DATA.L kRam+0x1A (the subroutine)
      0x0009,          // 018 NOP
      0xE608, 0x000B, 0x0009,  // 01A sub: MOV #8,R6 ; RTS ; NOP
  });
  s.start(System::kRam);
  s.r().r[14] = 0x12340000;
  s.r().r[13] = 0xCAFE0000;
  s.r().r[2] = 4;
  s.cpu().step();  // MOV.L
  CHECK_EQ(s.r().r[3], System::kRam + 0x1A);
  s.cpu().step();  // JSR @R3 + slot
  CHECK_EQ(s.r().r[1], 5u);
  CHECK_EQ(s.r().pc, System::kRam + 0x1A);
  CHECK_EQ(s.r().pr, System::kRam + 6);
  s.cpu().step();  // MOV #8
  s.cpu().step();  // RTS + slot
  CHECK_EQ(s.r().r[6], 8u);
  CHECK_EQ(s.r().pc, System::kRam + 6);
  s.cpu().step();  // LDC R14,GBR
  CHECK_EQ(s.r().gbr, 0x12340000u);
  s.cpu().step();  // LDS R13,PR
  CHECK_EQ(s.r().pr, 0xCAFE0000u);
  s.cpu().step();  // BRAF R2 + slot
  CHECK_EQ(s.r().pc, System::kRam + 0x12);
  s.cpu().step();
  CHECK_EQ(s.r().r[7], 9u);
  CHECK_EQ(disassemble(0x4E0B, 0), "JSR     @R14");
  CHECK_EQ(disassemble(0x0223, 0), "BRAF    R2");
}

void test_trapa_rte_and_interrupt() {
  System s;
  // Vector 32 (TRAPA #32) -> handler at 0x400; interrupt vector 70 (IRQ6) -> 0x500.
  s.bus().write32(32 * 4, 0x400);
  s.bus().write32(70 * 4, 0x500);
  s.code(System::kRam, {
      0xC320,  // TRAPA #H'20
      0xE001,  // MOV #1,R0        (return lands here)
      0x0009,  // NOP
      0x0009,  // NOP
  });
  s.code(0x400, {
      0xE202,  // MOV #2,R2
      0x002B,  // RTE
      0xE303,  // MOV #3,R3   (slot)
  });
  s.code(0x500, {
      0xE404,  // MOV #4,R4
      0x002B,  // RTE
      0x0009,  // NOP
  });
  s.start(System::kRam);
  const u32 sp = s.r().r[15];
  CHECK_EQ(s.cpu().step(), 11u);  // TRAPA: 8 + the vector read from 16-bit external memory (4 states, 3 beyond MA)
  CHECK_EQ(s.r().pc, 0x400u);
  CHECK_EQ(s.r().r[15], sp - 8);
  CHECK_EQ(s.bus().read32(sp - 4), 0u);  // SR pushed (mask cleared by start)
  CHECK_EQ(s.bus().read32(sp - 8), System::kRam + 2);
  s.cpu().step();  // MOV #2
  s.cpu().step();  // RTE + slot
  CHECK_EQ(s.r().r[3], 3u);
  CHECK_EQ(s.r().pc, System::kRam + 2);
  CHECK_EQ(s.r().r[15], sp);
  // Interrupt: level 5 vector 70 while mask 0.
  s.cpu().set_irq(5, 70, true);
  const u64 st = s.cpu().step();  // MOV #1 executes, then the interrupt is taken at the boundary
  CHECK_EQ(s.r().r[0], 1u);
  CHECK_EQ(s.r().pc, 0x500u);
  CHECK_EQ(s.cpu().interrupt_mask(), 5u);
  CHECK(s.cpu().irq_accepted());
  CHECK(st >= 1 + Cpu::kIrqStates + Cpu::kIrqPinExtra);
  s.cpu().set_irq(0, 0);
  s.cpu().step();  // MOV #4
  s.cpu().step();  // RTE
  CHECK_EQ(s.r().pc, System::kRam + 4);
  CHECK_EQ(s.cpu().interrupt_mask(), 0u);
  // Masked interrupt is not taken.
  s.r().sr |= Cpu::kIMask;
  s.cpu().set_irq(5, 70);
  s.cpu().step();
  CHECK_EQ(s.r().pc, System::kRam + 6);
}

void test_address_error_and_illegal() {
  System s;
  s.bus().write32(9 * 4, 0x600);   // CPU address error
  s.bus().write32(4 * 4, 0x700);   // general illegal
  s.bus().write32(6 * 4, 0x800);   // slot illegal
  s.code(System::kRam, {
      0x6152,  // MOV.L @R5,R1   with R5 odd -> address error after the instruction
      0x0009,
  });
  s.code(0x600, {0x0009});
  s.start(System::kRam);
  s.r().r[5] = 0x1001;
  s.cpu().step();
  CHECK_EQ(s.r().pc, 0x600u);
  CHECK_EQ(s.bus().read32(s.r().r[15]), System::kRam + 2);  // PC of the next instruction
  // Illegal instruction (FFFF): vector 4, PC = the illegal code itself.
  s.code(System::kRam + 0x20, {0xFFFF});
  s.start(System::kRam + 0x20);
  s.cpu().step();
  CHECK_EQ(s.r().pc, 0x700u);
  CHECK_EQ(s.bus().read32(s.r().r[15]), System::kRam + 0x20);
  // Illegal slot: a branch in a delay slot -> vector 6, PC = branch target.
  s.code(System::kRam + 0x40, {0xA001, 0xA000, 0x0009, 0x0009});  // BRA +2 ; slot BRA
  s.start(System::kRam + 0x40);
  s.cpu().step();
  CHECK_EQ(s.r().pc, 0x800u);
  CHECK_EQ(s.bus().read32(s.r().r[15]), System::kRam + 0x46);
}

void test_sleep_and_wake() {
  System s;
  s.bus().write32(70 * 4, 0x500);
  s.code(System::kRam, {0x001B, 0xE001});  // SLEEP ; MOV #1,R0
  s.code(0x500, {0x002B, 0x0009});
  s.start(System::kRam);
  s.cpu().step();
  CHECK(s.cpu().sleeping());
  const u64 t0 = s.cpu().total_states();
  s.cpu().run(1000);
  CHECK(s.cpu().sleeping());
  CHECK_EQ(s.cpu().total_states() - t0, 1000u);
  s.cpu().set_irq(3, 70);
  s.cpu().run(1);
  CHECK(!s.cpu().sleeping());
  CHECK_EQ(s.r().pc, 0x500u);
  CHECK_EQ(s.bus().read32(s.r().r[15]), System::kRam + 2);  // resumes after SLEEP
}

// Timing: on-chip code runs one state per instruction; external 16-bit code
// with wait states stalls per 4-byte fetch line; the cache removes it.
void test_fetch_timing() {
  System s;
  // 8 NOPs in on-chip RAM.
  s.code(System::kRam, {0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009});
  s.start(System::kRam);
  u64 t = 0;
  for (int i = 0; i < 8; ++i) t += s.cpu().step();
  CHECK_EQ(t, 8u);
  CHECK_EQ(s.r().pc, System::kRam + 16);
  // Same NOPs in CS0 (16-bit, 0 wait): a 4-byte fetch is two 2-state bus
  // cycles; two 1-state instructions hide 2 of them -> +2 per line.
  s.code(0x1000, {0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009});
  s.start(0x1000);
  t = 0;
  for (int i = 0; i < 8; ++i) t += s.cpu().step();
  CHECK_EQ(t, 8u + 4 * 2);
  // One wait state per bus cycle: a line costs 6, two instructions hide 2 -> +4 per line.
  s.start(0x1000);
  s.bus().set_class(Bus::kClsCs0, {16, 2, 1, true, true});  // after start(): reset reinstalls the BSC classes
  t = 0;
  for (int i = 0; i < 8; ++i) t += s.cpu().step();
  CHECK_EQ(t, 8u + 4 * 4);
  // 8-bit bus, 0 wait: a line is four 2-state cycles -> +6 per line.
  s.start(0x1000);
  s.bus().set_class(Bus::kClsCs0, {8, 2, 0, true, true});
  t = 0;
  for (int i = 0; i < 8; ++i) t += s.cpu().step();
  CHECK_EQ(t, 8u + 4 * 6);
  // Cache enabled for CS0 (16-bit, 1 wait): first pass misses every line
  // (fill 6 + 1 idle, consecutive misses no idle), a second pass hits.
  s.code(0x1010, {0xAFF6, 0x0009});  // BRA back to 0x1000 with a NOP slot: disp = (1000-1014)/2 = -10
  s.start(0x1000);
  s.bus().set_class(Bus::kClsCs0, {16, 2, 1, true, true});
  s.cpu().set_cache_control(0x01);
  t = 0;
  for (int i = 0; i < 8; ++i) t += s.cpu().step();
  // 4 lines: first miss 6+1, then 3 consecutive misses 6 each -> 8 + 7 + 18 = 33
  CHECK_EQ(t, 33u);
  s.cpu().step();  // BRA + slot (line 0x1010 miss)
  t = 0;
  for (int i = 0; i < 8; ++i) t += s.cpu().step();
  // Second pass: the first hit after a miss counts as a miss (6), the rest hit.
  CHECK_EQ(t, 8u + 6);
}

void test_data_access_timing() {
  System s;
  // MOV.L @R1,R0 from on-chip RAM data: 1 state; from CS0 16-bit 0-wait: two
  // 2-state bus cycles -> +3; with 2 waits: 2*4 = 8 -> +7; peripheral register
  // (16-bit, 2 states) word read: +1.
  s.code(System::kRam, {0x6012, 0x6012, 0x6012, 0x6011});
  s.start(System::kRam);
  s.r().r[1] = System::kRam + 0x800;
  CHECK_EQ(s.cpu().step(), 1u);
  s.r().r[1] = 0x2000;
  CHECK_EQ(s.cpu().step(), 4u);
  s.bus().set_class(Bus::kClsCs0, {16, 2, 2, true, true});
  CHECK_EQ(s.cpu().step(), 8u);
  s.r().r[1] = 0xFFFF8348u;  // MOV.W @R1,R0 from INTC IPRA
  CHECK_EQ(s.cpu().step(), 2u);
}

void test_self_modifying_code() {
  System s;
  s.code(System::kRam, {
      0xE001,  // MOV #1,R0
      0xE002,  // MOV #2,R0   <- will be overwritten with MOV #7,R0 by the program itself
      0x0009,
  });
  s.code(System::kRam + 0x10, {
      0x9102,          // MOV.W @(4,PC),R1   -> word at +4+4 = 0x1A? (0x10+4+4 = 0x18)
      0x2211,          // MOV.W R1,@R2       R2 = kRam+2
      0xA7F6,          // BRA kRam (disp -10*2)... compute: from 0x14: target = 0x14+4+d = 0 -> d = -0x18 -> 0xAFF4
      0x0009,          // slot NOP
      0xE007,          // .DATA.W MOV #7,R0
  });
  s.code(System::kRam + 0x14, {0xAFF4});
  s.start(System::kRam);
  s.cpu().step();
  s.cpu().step();
  CHECK_EQ(s.r().r[0], 2u);      // original decoded
  s.r().pc = System::kRam + 0x10;
  s.r().r[2] = System::kRam + 2;
  s.cpu().step();
  s.cpu().step();                // write into a code line -> cells dropped
  s.cpu().step();                // BRA + slot
  s.cpu().step();                // MOV #1
  s.cpu().step();                // re-decoded MOV #7
  CHECK_EQ(s.r().r[0], 7u);
}

// The delay slot is fused into the branch cell: the branch decides its
// target before the slot runs, the slot's fetch line is charged, and a write
// into the slot word or a bus retiming re-decodes the branch.
void test_fused_delay_slots() {
  System s;
  // JSR @R1 with a slot that overwrites R1: the target is the old R1.
  s.code(System::kRam, {
      0x410B,  // JSR @R1
      0xE100,  // MOV #0,R1   (slot)
      0x0009,
  });
  s.code(System::kRam + 0x20, {0x0009});
  s.start(System::kRam);
  s.r().r[1] = System::kRam + 0x20;
  s.cpu().step();
  CHECK_EQ(s.r().pc, System::kRam + 0x20);
  CHECK_EQ(s.r().r[1], 0u);
  CHECK_EQ(s.r().pr, System::kRam + 4);
  // RTS with LDS.L @R15+,PR in the slot returns to the old PR.
  s.code(System::kRam, {
      0x000B,  // RTS
      0x4F26,  // LDS.L @R15+,PR   (slot)
  });
  s.start(System::kRam);
  s.r().pr = System::kRam + 0x20;
  s.r().r[15] -= 4;
  s.bus().write32(s.r().r[15], 0x12345678);
  s.cpu().step();
  CHECK_EQ(s.r().pc, System::kRam + 0x20);
  CHECK_EQ(s.r().pr, 0x12345678u);
  CHECK_EQ(s.r().r[15], 0xFFFFFBF0u);
  // RTE pops before the slot runs (the slot sees the new SP) and restores SR after it.
  s.code(System::kRam, {
      0x002B,  // RTE
      0x63F3,  // MOV R15,R3 (slot)
  });
  s.start(System::kRam);
  s.r().r[15] -= 8;
  s.bus().write32(s.r().r[15], System::kRam + 0x20);  // PC
  s.bus().write32(s.r().r[15] + 4, 0x000000F1);       // SR: mask 15, T
  s.cpu().step();
  CHECK_EQ(s.r().pc, System::kRam + 0x20);
  CHECK_EQ(s.r().r[3], 0xFFFFFBF0u);
  CHECK_EQ(s.r().sr, 0xF1u);
  // BF/S not taken still executes the slot, then continues after it.
  s.code(System::kRam, {
      0x0018,  // SETT
      0x8F02,  // BF/S +4
      0xE005,  // MOV #5,R0   (slot)
      0xE006,  // MOV #6,R0
  });
  s.start(System::kRam);
  s.cpu().step();
  CHECK_EQ(s.cpu().step(), 2u);  // BF/S (1) + slot (1)
  CHECK_EQ(s.r().r[0], 5u);
  CHECK_EQ(s.r().pc, System::kRam + 6);
  s.cpu().step();
  CHECK_EQ(s.r().r[0], 6u);

  // Across a cell page boundary: CS0 RAM extended to 128 KB so code can sit
  // at 0xFFFC..0x10004.  BF/S at 0xFFFC falls through into the next page;
  // BRA at 0xFFFE has its slot in the next page; the target of both is in
  // another page than the branch.
  s.bus().map_ram(0x10000, 0x10000, Bus::kClsCs0);
  s.code(0xFFF8, {
      0x0018,  // SETT
      0x0009,
      0x8F01,  // 0xFFFC: BF/S +2 (not taken)
      0xE001,  // 0xFFFE: MOV #1,R0 (slot)
      0xE002,  // 0x10000: MOV #2,R1
      0xA004,  // 0x10002: BRA 0x1000E
      0xE103,  // 0x10004: MOV #3,R1 (slot)
  });
  s.code(0x1000E, {0x0009});
  s.start(0xFFF8);
  s.cpu().step();
  s.cpu().step();
  s.cpu().step();  // BF/S + slot
  CHECK_EQ(s.r().r[0], 1u);
  CHECK_EQ(s.r().pc, 0x10000u);
  s.cpu().step();
  s.cpu().step();  // BRA + slot
  CHECK_EQ(s.r().r[1], 3u);
  CHECK_EQ(s.r().pc, 0x1000Eu);
  s.code(0xFFFC, {
      0xA001,  // 0xFFFC: BRA 0x10002
      0xE104,  // 0xFFFE: MOV #4,R1 (slot, last word of the page)
  });
  s.code(0x10002, {0x0009});
  s.start(0xFFFC);
  s.cpu().step();
  CHECK_EQ(s.r().r[1], 4u);
  CHECK_EQ(s.r().pc, 0x10002u);

  // Fetch timing (CS0 16-bit, 0 wait: +2 per line).  A slot that starts a
  // line is charged with the branch; a direct target in the second word of a
  // line pays for that line at the branch.
  s.code(0x1000, {
      0x0009,  // 0x1000 NOP            line start: 1 + 2
      0xA001,  // 0x1002 BRA 0x1008     2, slot at 0x1004 starts a line: +2
      0x0009,  // 0x1004 NOP (slot)     1
      0x0009,  // 0x1006
      0xA000,  // 0x1008 BRA 0x100C     line start: 2 + 2, slot 1
      0x0009,  // 0x100A (slot)
      0xA000,  // 0x100C BRA 0x1010     line start: 2 + 2, slot 1
      0x0009,  // 0x100E (slot)
      0xA001,  // 0x1010 BRA 0x1016     line start: 2 + 2, slot 1, target line 0x1014: +2
      0x0009,  // 0x1012 (slot)
      0x0009,  // 0x1014
      0x0009,  // 0x1016 NOP            not a line start: 1
  });
  s.start(0x1000);
  CHECK_EQ(s.cpu().step(), 3u);
  CHECK_EQ(s.cpu().step(), 5u);
  CHECK_EQ(s.r().pc, 0x1008u);
  CHECK_EQ(s.cpu().step(), 5u);
  CHECK_EQ(s.cpu().step(), 5u);
  CHECK_EQ(s.cpu().step(), 7u);
  CHECK_EQ(s.r().pc, 0x1016u);
  CHECK_EQ(s.cpu().step(), 1u);
  // Retiming the area (one wait state: a line costs 6, +4 per line) takes
  // effect on already decoded cells.
  s.start(0x1000);
  CHECK_EQ(s.cpu().step(), 3u);
  s.bus().set_class(Bus::kClsCs0, {16, 2, 1, true, true});
  s.start(0x1000);
  s.bus().set_class(Bus::kClsCs0, {16, 2, 1, true, true});
  CHECK_EQ(s.cpu().step(), 5u);
  CHECK_EQ(s.cpu().step(), 2u + 1u + 4u);

  // A store into the slot word re-decodes the branch, also when the slot
  // starts a new attribute line (branch at +0x7E, slot at +0x80).
  s.code(System::kRam + 0x7E, {
      0xA001,  // BRA +2 -> kRam + 0x84
      0xE001,  // MOV #1,R0 (slot)  <- overwritten with MOV #9,R0
  });
  s.code(System::kRam + 0x84, {0x0009});
  s.code(System::kRam + 0x40, {
      0x2211,  // MOV.W R1,@R2
      0xA000,  // BRA +0 -> kRam + 0x46
      0x0009,
      0x0009,
  });
  s.start(System::kRam + 0x7E);
  s.cpu().step();
  CHECK_EQ(s.r().r[0], 1u);
  s.r().pc = System::kRam + 0x40;
  s.r().r[1] = 0xE009;
  s.r().r[2] = System::kRam + 0x80;
  s.cpu().step();
  s.r().pc = System::kRam + 0x7E;
  s.cpu().step();
  CHECK_EQ(s.r().r[0], 9u);
  CHECK_EQ(s.r().pc, System::kRam + 0x84);
}

void test_disassembler() {
  CHECK_EQ(disassemble(0xE07F, 0), "MOV     #127,R0");
  CHECK_EQ(disassemble(0xA002, 0x1000), "BRA     H'1008");
  CHECK_EQ(disassemble(0x8FFD, 0x1012), "BF/S    H'1010");
  CHECK_EQ(disassemble(0x6152, 0), "MOV.L   @R5,R1");
  CHECK_EQ(disassemble(0x415F, 0), "MAC.W   @R5+,@R1+");
  CHECK_EQ(disassemble(0xC320, 0), "TRAPA   #H'20");
  CHECK_EQ(disassemble(0x002B, 0), "RTE");
}

}  // namespace

int main() {
  test_reset_and_basic_moves();
  test_memory_addressing();
  test_alu_flags();
  test_division_and_multiply();
  test_mac_and_saturation();
  test_delayed_branches();
  test_register_fields();
  test_trapa_rte_and_interrupt();
  test_address_error_and_illegal();
  test_sleep_and_wake();
  test_fetch_timing();
  test_data_access_timing();
  test_self_modifying_code();
  test_fused_delay_slots();
  test_disassembler();
  return test::finish("test_sh2_cpu");
}

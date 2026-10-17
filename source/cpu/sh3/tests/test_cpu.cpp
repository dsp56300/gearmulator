// CPU tests: reset state, the P0-P3 aliases and byte order, arithmetic and
// flags, delayed branches, register banks and exceptions, misaligned
// accesses and the disassembler.
#include <initializer_list>

#include "common/test_util.hpp"
#include "cpu/sh3/disasm.hpp"
#include "cpu/sh3/machine.hpp"

using namespace sh3;

namespace {

constexpr u32 kRam = 0x0C000000u;   // physical SDRAM (area 3)
constexpr u32 kCode = 0x8C001000u;  // P1 alias of it
constexpr u32 kStack = 0x8C010000u;

struct System {
  Machine m;
  System() {
    m.map_rom(0x00000000, 0x10000);
    m.map_ram(kRam, 0x40000);
    m.reset();
    m.cpu().regs().pc = kCode;
    m.cpu().regs().r[15] = kStack;
    m.cpu().regs().vbr = 0x8C020000u;
    m.cpu().set_sr(kSrMD | kSrRB | kSrIMask);  // BL clear: exceptions / interrupts allowed
  }
  Cpu& cpu() { return m.cpu(); }
  void code(std::initializer_list<u16> words, u32 at = kCode) {
    u32 a = at;
    for (u16 w : words) { m.bus().write16(a, w); a += 2; }
    cpu().invalidate_range(at, u32(words.size() * 2));
  }
  void steps(unsigned n) { for (unsigned i = 0; i < n; ++i) m.step(); }
  u32 r(unsigned i) { return cpu().regs().r[i]; }
  bool t() { return cpu().regs().sr & kSrT; }
};

void test_reset_and_aliases() {
  Machine m;
  m.map_rom(0, 0x10000);
  m.map_ram(kRam, 0x1000);
  m.reset();
  CHECK_EQ(m.cpu().regs().pc, 0xA0000000u);
  CHECK_EQ(m.cpu().regs().sr, kSrMD | kSrRB | kSrBL | kSrIMask);
  CHECK_EQ(m.cpu().expevt(), 0u);
  m.bus().write32(0x8C000010u, 0x12345678u);
  CHECK_EQ(m.bus().read32(0x0C000010u), 0x12345678u);
  CHECK_EQ(m.bus().read32(0xAC000010u), 0x12345678u);
  CHECK_EQ(m.bus().read32(0xCC000010u), 0x12345678u);
  CHECK_EQ(m.bus().read8(0x0C000010u), 0x12);  // big-endian
  CHECK_EQ(m.bus().read16(0x0C000012u), 0x5678u);
  CHECK_EQ(m.bus().read32(0x0C000100u), 0u);
  CHECK_EQ(m.bus().read32(0x0D000000u), 0xFFFFFFFFu);  // unmapped
  // A ROM at the bottom of area 0 reads through every alias.
  const u8 rom[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  m.bus().load(0x100, rom, 4);
  CHECK_EQ(m.bus().read32(0x80000100u), 0xDEADBEEFu);
  CHECK_EQ(m.bus().read32(0xA0000100u), 0xDEADBEEFu);
  // On-chip registers: TRA through the CCN.
  m.bus().write32(0xFFFFFFD0u, 0x14u);
  CHECK_EQ(m.cpu().tra(), 0x14u);
  CHECK_EQ(m.bus().read32(0xFFFFFFD0u), 0x14u);
  CHECK_EQ(m.bus().read32(0xFFFFFFD4u), 0u);  // EXPEVT after power-on
}

void test_alu_and_branches() {
  System s;
  s.code({
      0xE005,          // MOV #5,R0
      0xE1FE,          // MOV #-2,R1
      0x301C,          // ADD R1,R0        -> 3
      0x3100,          // CMP/EQ R0,R1     -> T=0
      0x8B00,          // BF +0 (skip one)
      0xE0AA,          // MOV #-86,R0 (skipped)
      0x4008,          // SHLL2 R0         -> 12
      0xA002,          // BRA +4 (skip two)
      0x0009,          // NOP (slot)
      0xE0BB,          // skipped
      0xE0CC,          // skipped
      0x7001,          // ADD #1,R0
  });
  s.steps(3);
  CHECK_EQ(s.r(0), 3u);
  s.steps(2);  // CMP, BF taken
  CHECK_EQ(s.cpu().regs().pc, kCode + 12);
  s.steps(1);
  CHECK_EQ(s.r(0), 12u);
  s.steps(1);  // BRA + slot
  CHECK_EQ(s.cpu().regs().pc, kCode + 22);
  s.steps(1);
  CHECK_EQ(s.r(0), 13u);
  // Big-endian PC-relative loads.
  s.code({
      0x9003,          // MOV.W @(6,PC),R0     -> data at kCode+0x40+4+6
      0xD102,          // MOV.L @(8,PC),R1     -> (pc+4)&~3 + 8
      0x0009, 0x0009,
      0x1234, 0x5678,  // .DATA.L H'12345678
      0xABCD, 0xEF01,
  }, kCode + 0x40);
  s.cpu().regs().pc = kCode + 0x40;
  s.steps(2);
  CHECK_EQ(s.r(0), 0x5678u);
  CHECK_EQ(s.r(1), 0xABCDEF01u);
}

void test_shad_shld_and_mac() {
  System s;
  s.code({
      0xE010,  // MOV #16,R0
      0xE1FC,  // MOV #-4,R1
      0xE280,  // MOV #-128,R2
      0x421C,  // SHAD R1,R2   -> -128 >> 4 = -8
      0x420D,  // SHLD R0,R2   -> -8 << 16
  });
  s.steps(5);
  CHECK_EQ(s.r(2), 0xFFF80000u);
  // DMULS.L
  s.code({
      0xE0FE,  // MOV #-2,R0
      0xE103,  // MOV #3,R1
      0x301D,  // DMULS.L R1,R0 -> -6
  }, kCode + 0x40);
  s.cpu().regs().pc = kCode + 0x40;
  s.steps(3);
  CHECK_EQ(s.cpu().regs().macl, 0xFFFFFFFAu);
  CHECK_EQ(s.cpu().regs().mach, 0xFFFFFFFFu);
}

// MAC.W / MAC.L with S set.  MAC.L clamps the 48-bit accumulator in two's
// complement, so a negative overflow leaves H'FFFF8000 in MACH: the result
// QEMU's test measured on an SH7091 against the SH-4 core manual, and the
// 48-bit range the SH-3 manual gives.  (The SH-2 manual's pseudo-code leaves
// H'00008000; the sh2 core follows that.)
void test_mac_saturation() {
  System s;
  s.m.bus().write16(0x8C002000u, 0x7FFF);
  s.m.bus().write16(0x8C002002u, 0x7FFF);
  s.m.bus().write32(0x8C002010u, 0x80000000u);
  s.m.bus().write32(0x8C002014u, 0x7FFFFFFFu);
  s.code({
      0xD002,  // MOV.L @(8,PC),R0    ; 8C002000
      0xD103,  // MOV.L @(12,PC),R1   ; 8C002002
      0x0058,  // SETS
      0x410F,  // MAC.W @R0+,@R1+     ; 7FFF * 7FFF
      0x0009, 0x0009,
      0x8C00, 0x2000,
      0x8C00, 0x2002,
  });
  s.cpu().regs().macl = 0x7FFFFFF0u;
  s.cpu().regs().mach = 0;
  s.steps(4);
  CHECK_EQ(s.cpu().regs().macl, 0x7FFFFFFFu);  // 32-bit saturation
  CHECK_EQ(s.cpu().regs().mach & 1, 1u);        // overflow flag
  // MAC.L: 48-bit saturation, MACH = H'FFFF8000 / MACL = 0 on negative overflow.
  s.code({
      0xD001,  // MOV.L @(4,PC),R0    ; 8C002010
      0xD102,  // MOV.L @(8,PC),R1    ; 8C002014
      0x010F,  // MAC.L @R0+,@R1+     ; -2^31 * (2^31 - 1)
      0x0009,
      0x8C00, 0x2010,
      0x8C00, 0x2014,
  }, kCode + 0x40);
  s.cpu().regs().pc = kCode + 0x40;
  s.cpu().regs().mach = 0xFFFF8000u;  // -2^47
  s.cpu().regs().macl = 0;
  s.steps(3);
  CHECK_EQ(s.cpu().regs().mach, 0xFFFF8000u);
  CHECK_EQ(s.cpu().regs().macl, 0u);
  // Positive overflow from the top of the range.
  s.cpu().regs().pc = kCode + 0x40;
  s.cpu().regs().mach = 0x00007FFFu;
  s.cpu().regs().macl = 0xFFFFFFF0u;
  s.m.bus().write32(0x8C002010u, 0x7FFFFFFFu);  // (2^31 - 1)^2 is positive
  s.steps(3);
  CHECK_EQ(s.cpu().regs().mach, 0x00007FFFu);
  CHECK_EQ(s.cpu().regs().macl, 0xFFFFFFFFu);
}

void test_memory_ops() {
  System s;
  s.code({
      0xD004,          // MOV.L @(16,PC),R0   ; buffer
      0xE17B,          // MOV #H'7B,R1
      0x2010,          // MOV.B R1,@R0
      0x6200,          // MOV.B @R0,R2        ; sign-extended
      0x6300,          // MOV.B @R0,R3
      0x2011,          // MOV.W R1,@R0
      0x6402,          // MOV.L @R0,R4
      0x0009,
      0x0009, 0x0009,
      0x8C00, 0x2000,  // 0x8C002000
  });
  s.steps(4);
  CHECK_EQ(s.r(2), 0x7Bu);
  CHECK_EQ(s.m.bus().read8(0x8C002000u), 0x7Bu);
  s.steps(3);
  // MOV.W wrote 007B big-endian over the byte; the longword read sees it
  // followed by the untouched (zero) halfword.
  CHECK_EQ(s.m.bus().read16(0x8C002000u), 0x007Bu);
  CHECK_EQ(s.r(4), 0x007B0000u);
}

void test_trapa_rte_and_banks() {
  System s;
  const u32 vbr = s.cpu().regs().vbr;
  // Handler at VBR+0x100: read TRA into R1 of the exception bank, RTE.
  s.code({
      0xD102,  // MOV.L @(8,PC),R1    ; TRA address
      0x6112,  // MOV.L @R1,R1
      0x002B,  // RTE
      0x0009,  // NOP
      0x0000, 0x0000,
      0xFFFF, 0xFFD0,  // 0xFFFFFFD0 (TRA)
  }, vbr + 0x100);
  s.code({
      0xE007,  // MOV #7,R0
      0xC305,  // TRAPA #5
      0x7001,  // ADD #1,R0
  });
  s.cpu().set_sr(kSrMD | kSrIMask);  // RB = 0: the handler runs on bank 1
  s.cpu().regs().r[1] = 0x11111111u;
  s.steps(2);  // MOV, TRAPA
  CHECK_EQ(s.cpu().regs().pc, vbr + 0x100);
  CHECK(s.cpu().regs().sr & kSrRB);
  CHECK(s.cpu().regs().sr & kSrBL);
  CHECK_EQ(s.cpu().expevt(), kExcTrapa);
  CHECK_EQ(s.cpu().regs().spc, kCode + 4);
  CHECK_EQ(s.cpu().tra(), 5u << 2);
  s.steps(2);
  CHECK_EQ(s.r(1), 5u << 2);               // bank 1's R1
  CHECK_EQ(s.cpu().bank(1), 0x11111111u);  // bank 0's R1 preserved
  s.steps(1);  // RTE + slot
  CHECK_EQ(s.cpu().regs().pc, kCode + 4);
  CHECK(!(s.cpu().regs().sr & kSrRB));
  CHECK_EQ(s.r(1), 0x11111111u);
  s.steps(1);
  CHECK_EQ(s.r(0), 8u);
}

void test_illegal_and_slot_illegal() {
  System s;
  const u32 vbr = s.cpu().regs().vbr;
  s.code({0x0009, 0xAFFE, 0x0009}, vbr + 0x100);  // NOP; BRA self
  s.code({0xF00C});  // FMOV: an SH-4 code, illegal here
  s.steps(1);
  CHECK_EQ(s.cpu().regs().pc, vbr + 0x100);
  CHECK_EQ(s.cpu().expevt(), kExcIllegal);
  CHECK_EQ(s.cpu().regs().spc, kCode);
  System t;
  t.code({0x0009, 0xAFFE, 0x0009}, t.cpu().regs().vbr + 0x100);
  t.code({0xA001, 0xA000});  // BRA with a BRA in its slot
  t.steps(1);
  CHECK_EQ(t.cpu().expevt(), kExcSlotIllegal);
  CHECK_EQ(t.cpu().regs().spc, kCode);
}

void test_misaligned_access() {
  System s;
  s.m.bus().write32(0x8C002000u, 0xAABBCCDDu);
  s.code({
      0xD001,  // MOV.L @(4,PC),R0
      0x6102,  // MOV.L @R0,R1     ; odd address: masked to the longword
      0x0009, 0x0009,
      0x8C00, 0x2001,
  });
  s.steps(2);
  CHECK_EQ(s.r(1), 0xAABBCCDDu);
  CHECK_EQ(s.cpu().misaligned_accesses(), 1u);
}

void test_sleep_wakes_on_interrupt() {
  System s;
  s.cpu().set_sr(kSrMD);  // IMASK 0
  const u32 vbr = s.cpu().regs().vbr;
  // Handler: clear TMU0's UNF (else the level re-enters right after RTE), count, return.
  s.code({0xD102, 0xE020, 0x2101, 0x7701, 0x002B, 0x0009, 0xFFFF, 0xFE9C}, vbr + 0x600);
  s.code({0x001B, 0x7001, 0xAFFE, 0x0009});       // SLEEP; ADD #1,R0; BRA self
  s.steps(1);
  CHECK(s.cpu().sleeping());
  s.m.run(100);
  CHECK(s.cpu().sleeping());
  CHECK_EQ(s.r(0), 0u);
  // A timer interrupt ends the sleep and continues after the SLEEP.
  s.m.bus().write16(0xFFFFFEE2u, 0xF000);  // IPRA: TMU0 priority 15
  s.m.bus().write32(0xFFFFFE94u, 2);       // TCOR0
  s.m.bus().write32(0xFFFFFE98u, 2);       // TCNT0
  s.m.bus().write16(0xFFFFFE9Cu, 0x20);    // TCR0: UNIE, P/4
  s.m.bus().write8(0xFFFFFE92u, 1);        // TSTR
  s.m.run(200);
  CHECK(!s.cpu().sleeping());
  // The timer keeps firing, so the CPU may sit inside the handler (bank 1) now.
  const u32 r0 = (s.cpu().regs().sr & kSrRB) ? s.cpu().bank(0) : s.r(0);
  CHECK_EQ(r0, 1u);
  CHECK_EQ(s.cpu().intevt2(), 0x400u);
}

void test_disassembly() {
  CHECK_EQ(disassemble(u16(0xE005), 0), "MOV     #5,R0");
  CHECK_EQ(disassemble(u16(0x421C), 0), "SHAD    R1,R2");
  CHECK_EQ(disassemble(u16(0x0083), 0), "PREF    @R0");
  CHECK_EQ(disassemble(u16(0x4087), 0), "LDC.L   @R0+,R0_BANK");
  CHECK_EQ(disassemble(u16(0x01B2), 0), "STC     R3_BANK,R1");
  CHECK_EQ(disassemble(u16(0x403E), 0), "LDC     R0,SSR");
  CHECK_EQ(disassemble(u16(0xD003), 0x80000000u), "MOV.L   @(H'C,PC),R0   ; H'80000010");
  CHECK_EQ(disassemble(u16(0xF00C), 0), ".DATA.W (invalid)");
  CHECK_EQ(disassemble(u16(0xFBFD), 0), ".DATA.W (invalid)");
}

}  // namespace

int main() {
  test_reset_and_aliases();
  test_alu_and_branches();
  test_shad_shld_and_mac();
  test_mac_saturation();
  test_memory_ops();
  test_trapa_rte_and_banks();
  test_illegal_and_slot_illegal();
  test_misaligned_access();
  test_sleep_wakes_on_interrupt();
  test_disassembly();
  return test::finish("sh3_test_cpu");
}

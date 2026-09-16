// CPU execution tests: register file, timing per variant, flags, calls,
// interrupt entry, addressing modes, signed arithmetic, shifts, block moves,
// the KB auxiliary PSW and the unimplemented-opcode interrupt.
#include <initializer_list>

#include "cpu/mcs96/machine.hpp"
#include "common/test_util.hpp"

using namespace mcs96;

namespace {

struct System {
  Machine m;
  explicit System(Variant v = Variant::I80C196KB) : m(v) {
    m.bus().map_ram(0x0000, 0x10000);
    m.reset();
  }
  Cpu& cpu() { return m.cpu(); }
  u8* mem() { return m.bus().mem(); }
  void code(u16 at, std::initializer_list<u8> bytes) {
    u16 a = at;
    for (u8 b : bytes) mem()[a++] = b;
    cpu().invalidate_range(at, u32(bytes.size()));
  }
  void code(std::initializer_list<u8> bytes) { code(cpu().regs().pc, bytes); }
};

void test_reset_and_register_file() {
  System s;
  Cpu& c = s.cpu();
  CHECK_EQ(c.regs().pc, kResetPc);
  CHECK_EQ(c.regs().psw, 0);
  CHECK_EQ(c.total_states(), 0u);
  CHECK_EQ(c.reg8(0x20), 0xFF);
  c.set_reg16(0x20, 0x1234);
  CHECK_EQ(c.reg8(0x20), 0x34);
  CHECK_EQ(c.reg8(0x21), 0x12);
  CHECK_EQ(c.reg16(0x20), 0x1234);
  c.set_reg16(0x21, 0xABCD);
  CHECK_EQ(c.reg16(0x20), 0xABCD);
  CHECK_EQ(c.reg16(0x21), 0xABCD);
  c.set_reg16(0x00, 0xFFFF);
  CHECK_EQ(c.reg16(0x00), 0);
  c.set_psw(kZ | 0x005A);
  CHECK_EQ(c.reg8(0x08), 0x5A);
  c.set_reg8(0x08, 0xA5);
  CHECK_EQ(c.regs().psw, kZ | 0x00A5);
}

void test_variant_fixed_timing() {
  System kb(Variant::I80C196KB), x9x(Variant::I8x9x);
  kb.code({0xFD, 0x00, 0xAA});
  x9x.code({0xFD, 0x00, 0xAA});
  CHECK_EQ(kb.cpu().step(), 2u);
  CHECK_EQ(x9x.cpu().step(), 4u);
  CHECK_EQ(kb.cpu().regs().pc, kResetPc + 1);
  CHECK_EQ(kb.cpu().step(), 3u);  // SKIP
  CHECK_EQ(x9x.cpu().step(), 4u);
  CHECK_EQ(kb.cpu().regs().pc, kResetPc + 3);
  CHECK_EQ(x9x.cpu().regs().pc, kResetPc + 3);
}

void test_flags_and_conditional_branch() {
  System s;
  s.code({0xF9, 0xF8, 0xDF, 0x02});
  CHECK_EQ(s.cpu().step(), 2u);
  CHECK((s.cpu().regs().psw & kC) != 0);
  CHECK_EQ(s.cpu().step(), 2u);
  CHECK((s.cpu().regs().psw & kC) == 0);
  s.cpu().set_psw(kZ);
  CHECK_EQ(s.cpu().step(), 8u);
  CHECK_EQ(s.cpu().regs().pc, kResetPc + 6);
}

void test_call_and_return(Variant v, unsigned call_states, unsigned ret_states) {
  System s(v);
  s.cpu().set_sp(0x0080);
  const u16 pc = s.cpu().regs().pc;
  s.code({0x28, 0x02, 0x00, 0x00, 0xF0});
  CHECK_EQ(s.cpu().step(), call_states);
  CHECK_EQ(s.cpu().regs().pc, pc + 4);
  CHECK_EQ(s.cpu().sp(), 0x007E);
  CHECK_EQ(s.cpu().reg16(0x7E), pc + 2);
  CHECK_EQ(s.cpu().step(), ret_states);
  CHECK_EQ(s.cpu().regs().pc, pc + 2);
  CHECK_EQ(s.cpu().sp(), 0x0080);
}

void test_interrupt_entry() {
  System s;
  s.cpu().set_sp(0x0080);
  s.mem()[0x2000] = 0x34;
  s.mem()[0x2001] = 0x12;
  s.cpu().set_psw(kI | 0x01);   // TIMER enabled
  s.m.periph().request(Peripherals::IrqSrc::Timer);
  CHECK_EQ(s.cpu().step(), 16u);
  CHECK_EQ(s.cpu().regs().pc, 0x1234);
  CHECK_EQ(s.cpu().sp(), 0x007E);
  CHECK_EQ(s.cpu().reg16(0x7E), kResetPc);
  CHECK_EQ(s.m.periph().interrupt_pending_low(), 0);
  // The first ISR instruction runs before another request is honoured.
  s.mem()[0x1234] = 0xFD;
  s.mem()[0x1235] = 0xFD;
  s.m.periph().request(Peripherals::IrqSrc::Timer);
  CHECK_EQ(s.cpu().step(), 2u);
  CHECK_EQ(s.cpu().regs().pc, 0x1235);
  CHECK_EQ(s.cpu().step(), 16u);
}

void test_arithmetic_and_addressing_modes() {
  System s;
  const u16 pc = s.cpu().regs().pc;
  s.cpu().set_reg16(0x20, 0x7FFF);
  s.cpu().set_reg16(0x22, 1);
  s.code({0x44, 0x20, 0x22, 0x24});  // ADD 24,22,20
  CHECK_EQ(s.cpu().step(), 5u);
  CHECK_EQ(s.cpu().reg16(0x24), 0x8000);
  CHECK((s.cpu().regs().psw & kV) != 0);
  CHECK((s.cpu().regs().psw & kVt) != 0);
  CHECK((s.cpu().regs().psw & kN) == 0);

  s.cpu().set_reg16(0x20, 0x0300);
  s.mem()[0x0300] = 0x34;
  s.mem()[0x0301] = 0x12;
  s.code(u16(pc + 4), {0xA2, 0x21, 0x22});  // LD 22,[20]+
  CHECK_EQ(s.cpu().step(), 8u);
  CHECK_EQ(s.cpu().reg16(0x22), 0x1234);
  CHECK_EQ(s.cpu().reg16(0x20), 0x0302);

  s.cpu().set_reg16(0x20, 0x0400);
  s.mem()[0x0410] = 0x78;
  s.mem()[0x0411] = 0x56;
  s.code(u16(pc + 7), {0xA3, 0x21, 0x10, 0x00, 0x24});  // LD 24,10h[20] (long)
  CHECK_EQ(s.cpu().step(), 10u);
  CHECK_EQ(s.cpu().reg16(0x24), 0x5678);
}

void test_signed_math_shift_and_block_move() {
  System s;
  const u16 pc = s.cpu().regs().pc;
  s.cpu().set_reg16(0x24, 0xFFFD);
  s.code({0xFE, 0x6D, 0xFE, 0xFF, 0x24});  // MUL 24,#-2 (signed)
  CHECK_EQ(s.cpu().step(), 17u);
  CHECK_EQ(s.cpu().reg16(0x24), 6);
  CHECK_EQ(s.cpu().reg16(0x26), 0);

  s.cpu().set_reg16(0x20, 0x4000);
  s.code(u16(pc + 5), {0x09, 1, 0x20});  // SHL 20,#1
  CHECK_EQ(s.cpu().step(), 7u);
  CHECK_EQ(s.cpu().reg16(0x20), 0x8000);
  CHECK((s.cpu().regs().psw & kV) != 0);

  s.cpu().set_reg16(0x20, 2);
  s.cpu().set_reg16(0x24, 0x0300);
  s.cpu().set_reg16(0x26, 0x0400);
  for (unsigned i = 0; i < 4; ++i) s.mem()[0x0300 + i] = u8(0x11 * (i + 1));
  s.code(u16(pc + 8), {0xC1, 0x20, 0x24});  // BMOV
  CHECK_EQ(s.cpu().step(), 34u);
  CHECK_EQ(s.mem()[0x0400], 0x11);
  CHECK_EQ(s.mem()[0x0403], 0x44);
  CHECK_EQ(s.cpu().reg16(0x24), 0x0304);
  CHECK_EQ(s.cpu().reg16(0x26), 0x0404);
}

void test_kb_auxiliary_psw_and_illegal_opcode() {
  System s;
  s.cpu().set_sp(0x0080);
  s.cpu().set_psw(u16(kI | kC | 0x35));
  s.cpu().set_reg8(0x13, 0x22);
  s.cpu().set_reg8(0x14, 0xAF);
  s.code({0xF4, 0xF5});
  CHECK_EQ(s.cpu().step(), 12u);
  CHECK_EQ(s.cpu().sp(), 0x007C);
  CHECK_EQ(s.cpu().regs().psw, 0);
  CHECK_EQ(s.cpu().reg8(0x13), 0);
  CHECK_EQ(s.cpu().reg8(0x14), 0);
  CHECK_EQ(s.cpu().step(), 12u);
  CHECK_EQ(s.cpu().sp(), 0x0080);
  CHECK_EQ(s.cpu().regs().psw, u16(kI | kC | 0x35));
  CHECK_EQ(s.cpu().reg8(0x14), 0xAF);
  s.cpu().set_reg8(0x14, 0);
  CHECK_EQ(s.cpu().reg8(0x13), 0x22);

  s.m.reset();
  s.cpu().set_sp(0x0080);
  s.code(kResetPc, {0x04});
  s.mem()[kUnimplementedOpcodeVector] = 0x78;
  s.mem()[kUnimplementedOpcodeVector + 1] = 0x56;
  s.cpu().invalidate_all();
  CHECK_EQ(s.cpu().step(), 16u);
  CHECK_EQ(s.cpu().regs().pc, 0x5678);
  CHECK_EQ(s.cpu().reg16(0x7E), kResetPc + 1);
  CHECK_EQ(s.cpu().illegal_instructions(), 1u);
}

void test_code_invalidation() {
  System s;
  s.code({0xB1, 0x01, 0x20, 0x27, 0xFB});  // LDB 20,#1 ; SJMP $-5
  s.cpu().step();
  CHECK_EQ(s.cpu().reg8(0x20), 1);
  s.cpu().step();
  s.m.bus().write8(kResetPc + 1, 0x02);
  s.cpu().step();
  CHECK_EQ(s.cpu().reg8(0x20), 2);
}

}  // namespace

int main() {
  test_reset_and_register_file();
  test_variant_fixed_timing();
  test_flags_and_conditional_branch();
  test_call_and_return(Variant::I80C196KB, 11, 11);
  test_call_and_return(Variant::I8x9x, 13, 12);
  test_interrupt_entry();
  test_arithmetic_and_addressing_modes();
  test_signed_math_shift_and_block_move();
  test_kb_auxiliary_psw_and_illegal_opcode();
  test_code_invalidation();
  return test::finish("mcs96 cpu");
}

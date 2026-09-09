#include "cpu/sh2/disasm.hpp"

#include <cstdio>

namespace sh2 {

namespace {
std::string hex(u32 v) {
  char b[16];
  std::snprintf(b, sizeof b, "H'%X", v);
  return b;
}
std::string reg(unsigned r) { return "R" + std::to_string(r); }
}  // namespace

std::string disassemble(u16 code, u32 pc) { return disassemble(decode(code), pc); }

std::string disassemble(const DecodedInsn& d, u32 pc) {
  const std::string rn = reg(d.n), rm = reg(d.m);
  const u32 pc4 = pc + 4;
  const s32 i = d.imm;
  auto imm8 = [&] { return "#" + hex(u32(i) & 0xFF); };
  auto disp = [&] { return hex(u32(i)); };
  auto rel = [&] { return hex(pc4 + u32(i)); };
  switch (d.op) {
    case Op::MovI: return "MOV     #" + std::to_string(i) + "," + rn;
    case Op::MovwPc: return "MOV.W   @(" + disp() + ",PC)," + rn + "   ; " + hex(pc4 + u32(i));
    case Op::MovlPc: return "MOV.L   @(" + disp() + ",PC)," + rn + "   ; " + hex((pc4 & ~3u) + u32(i));
    case Op::MovR: return "MOV     " + rm + "," + rn;
    case Op::MovbS: return "MOV.B   " + rm + ",@" + rn;
    case Op::MovwS: return "MOV.W   " + rm + ",@" + rn;
    case Op::MovlS: return "MOV.L   " + rm + ",@" + rn;
    case Op::MovbL: return "MOV.B   @" + rm + "," + rn;
    case Op::MovwL: return "MOV.W   @" + rm + "," + rn;
    case Op::MovlL: return "MOV.L   @" + rm + "," + rn;
    case Op::MovbM: return "MOV.B   " + rm + ",@-" + rn;
    case Op::MovwM: return "MOV.W   " + rm + ",@-" + rn;
    case Op::MovlM: return "MOV.L   " + rm + ",@-" + rn;
    case Op::MovbP: return "MOV.B   @" + rm + "+," + rn;
    case Op::MovwP: return "MOV.W   @" + rm + "+," + rn;
    case Op::MovlP: return "MOV.L   @" + rm + "+," + rn;
    case Op::MovbS4: return "MOV.B   R0,@(" + disp() + "," + rn + ")";
    case Op::MovwS4: return "MOV.W   R0,@(" + disp() + "," + rn + ")";
    case Op::MovlS4: return "MOV.L   " + rm + ",@(" + disp() + "," + rn + ")";
    case Op::MovbL4: return "MOV.B   @(" + disp() + "," + rm + "),R0";
    case Op::MovwL4: return "MOV.W   @(" + disp() + "," + rm + "),R0";
    case Op::MovlL4: return "MOV.L   @(" + disp() + "," + rm + ")," + rn;
    case Op::MovbS0: return "MOV.B   " + rm + ",@(R0," + rn + ")";
    case Op::MovwS0: return "MOV.W   " + rm + ",@(R0," + rn + ")";
    case Op::MovlS0: return "MOV.L   " + rm + ",@(R0," + rn + ")";
    case Op::MovbL0: return "MOV.B   @(R0," + rm + ")," + rn;
    case Op::MovwL0: return "MOV.W   @(R0," + rm + ")," + rn;
    case Op::MovlL0: return "MOV.L   @(R0," + rm + ")," + rn;
    case Op::MovbSG: return "MOV.B   R0,@(" + disp() + ",GBR)";
    case Op::MovwSG: return "MOV.W   R0,@(" + disp() + ",GBR)";
    case Op::MovlSG: return "MOV.L   R0,@(" + disp() + ",GBR)";
    case Op::MovbLG: return "MOV.B   @(" + disp() + ",GBR),R0";
    case Op::MovwLG: return "MOV.W   @(" + disp() + ",GBR),R0";
    case Op::MovlLG: return "MOV.L   @(" + disp() + ",GBR),R0";
    case Op::Mova: return "MOVA    @(" + disp() + ",PC),R0   ; " + hex((pc4 & ~3u) + u32(i));
    case Op::Movt: return "MOVT    " + rn;
    case Op::SwapB: return "SWAP.B  " + rm + "," + rn;
    case Op::SwapW: return "SWAP.W  " + rm + "," + rn;
    case Op::Xtrct: return "XTRCT   " + rm + "," + rn;
    case Op::Add: return "ADD     " + rm + "," + rn;
    case Op::AddI: return "ADD     #" + std::to_string(i) + "," + rn;
    case Op::AddC: return "ADDC    " + rm + "," + rn;
    case Op::AddV: return "ADDV    " + rm + "," + rn;
    case Op::CmpEqI: return "CMP/EQ  #" + std::to_string(i) + ",R0";
    case Op::CmpEq: return "CMP/EQ  " + rm + "," + rn;
    case Op::CmpHs: return "CMP/HS  " + rm + "," + rn;
    case Op::CmpGe: return "CMP/GE  " + rm + "," + rn;
    case Op::CmpHi: return "CMP/HI  " + rm + "," + rn;
    case Op::CmpGt: return "CMP/GT  " + rm + "," + rn;
    case Op::CmpPl: return "CMP/PL  " + rn;
    case Op::CmpPz: return "CMP/PZ  " + rn;
    case Op::CmpStr: return "CMP/STR " + rm + "," + rn;
    case Op::Div1: return "DIV1    " + rm + "," + rn;
    case Op::Div0S: return "DIV0S   " + rm + "," + rn;
    case Op::Div0U: return "DIV0U";
    case Op::DmulsL: return "DMULS.L " + rm + "," + rn;
    case Op::DmuluL: return "DMULU.L " + rm + "," + rn;
    case Op::Dt: return "DT      " + rn;
    case Op::ExtsB: return "EXTS.B  " + rm + "," + rn;
    case Op::ExtsW: return "EXTS.W  " + rm + "," + rn;
    case Op::ExtuB: return "EXTU.B  " + rm + "," + rn;
    case Op::ExtuW: return "EXTU.W  " + rm + "," + rn;
    case Op::MacL: return "MAC.L   @" + rm + "+,@" + rn + "+";
    case Op::MacW: return "MAC.W   @" + rm + "+,@" + rn + "+";
    case Op::MulL: return "MUL.L   " + rm + "," + rn;
    case Op::MulsW: return "MULS.W  " + rm + "," + rn;
    case Op::MuluW: return "MULU.W  " + rm + "," + rn;
    case Op::Neg: return "NEG     " + rm + "," + rn;
    case Op::NegC: return "NEGC    " + rm + "," + rn;
    case Op::Sub: return "SUB     " + rm + "," + rn;
    case Op::SubC: return "SUBC    " + rm + "," + rn;
    case Op::SubV: return "SUBV    " + rm + "," + rn;
    case Op::And: return "AND     " + rm + "," + rn;
    case Op::AndI: return "AND     " + imm8() + ",R0";
    case Op::AndB: return "AND.B   " + imm8() + ",@(R0,GBR)";
    case Op::Not: return "NOT     " + rm + "," + rn;
    case Op::Or: return "OR      " + rm + "," + rn;
    case Op::OrI: return "OR      " + imm8() + ",R0";
    case Op::OrB: return "OR.B    " + imm8() + ",@(R0,GBR)";
    case Op::Tas: return "TAS.B   @" + rn;
    case Op::Tst: return "TST     " + rm + "," + rn;
    case Op::TstI: return "TST     " + imm8() + ",R0";
    case Op::TstB: return "TST.B   " + imm8() + ",@(R0,GBR)";
    case Op::Xor: return "XOR     " + rm + "," + rn;
    case Op::XorI: return "XOR     " + imm8() + ",R0";
    case Op::XorB: return "XOR.B   " + imm8() + ",@(R0,GBR)";
    case Op::Rotl: return "ROTL    " + rn;
    case Op::Rotr: return "ROTR    " + rn;
    case Op::Rotcl: return "ROTCL   " + rn;
    case Op::Rotcr: return "ROTCR   " + rn;
    case Op::Shal: return "SHAL    " + rn;
    case Op::Shar: return "SHAR    " + rn;
    case Op::Shll: return "SHLL    " + rn;
    case Op::Shlr: return "SHLR    " + rn;
    case Op::Shll2: return "SHLL2   " + rn;
    case Op::Shlr2: return "SHLR2   " + rn;
    case Op::Shll8: return "SHLL8   " + rn;
    case Op::Shlr8: return "SHLR8   " + rn;
    case Op::Shll16: return "SHLL16  " + rn;
    case Op::Shlr16: return "SHLR16  " + rn;
    case Op::Bf: return "BF      " + rel();
    case Op::BfS: return "BF/S    " + rel();
    case Op::Bt: return "BT      " + rel();
    case Op::BtS: return "BT/S    " + rel();
    case Op::Bra: return "BRA     " + rel();
    case Op::Braf: return "BRAF    " + rm;
    case Op::Bsr: return "BSR     " + rel();
    case Op::Bsrf: return "BSRF    " + rm;
    case Op::Jmp: return "JMP     @" + rm;
    case Op::Jsr: return "JSR     @" + rm;
    case Op::Rts: return "RTS";
    case Op::Clrt: return "CLRT";
    case Op::Clrmac: return "CLRMAC";
    case Op::LdcSr: return "LDC     " + rm + ",SR";
    case Op::LdcGbr: return "LDC     " + rm + ",GBR";
    case Op::LdcVbr: return "LDC     " + rm + ",VBR";
    case Op::LdclSr: return "LDC.L   @" + rm + "+,SR";
    case Op::LdclGbr: return "LDC.L   @" + rm + "+,GBR";
    case Op::LdclVbr: return "LDC.L   @" + rm + "+,VBR";
    case Op::LdsMach: return "LDS     " + rm + ",MACH";
    case Op::LdsMacl: return "LDS     " + rm + ",MACL";
    case Op::LdsPr: return "LDS     " + rm + ",PR";
    case Op::LdslMach: return "LDS.L   @" + rm + "+,MACH";
    case Op::LdslMacl: return "LDS.L   @" + rm + "+,MACL";
    case Op::LdslPr: return "LDS.L   @" + rm + "+,PR";
    case Op::Nop: return "NOP";
    case Op::Rte: return "RTE";
    case Op::Sett: return "SETT";
    case Op::Sleep: return "SLEEP";
    case Op::StcSr: return "STC     SR," + rn;
    case Op::StcGbr: return "STC     GBR," + rn;
    case Op::StcVbr: return "STC     VBR," + rn;
    case Op::StclSr: return "STC.L   SR,@-" + rn;
    case Op::StclGbr: return "STC.L   GBR,@-" + rn;
    case Op::StclVbr: return "STC.L   VBR,@-" + rn;
    case Op::StsMach: return "STS     MACH," + rn;
    case Op::StsMacl: return "STS     MACL," + rn;
    case Op::StsPr: return "STS     PR," + rn;
    case Op::StslMach: return "STS.L   MACH,@-" + rn;
    case Op::StslMacl: return "STS.L   MACL,@-" + rn;
    case Op::StslPr: return "STS.L   PR,@-" + rn;
    case Op::Trapa: return "TRAPA   " + imm8();
    default: return ".DATA.W (invalid)";
  }
}

}  // namespace sh2

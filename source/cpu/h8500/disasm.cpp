#include "cpu/h8500/disasm.hpp"

#include <cstdio>

namespace h8500 {

const char* op_name(Op op) {
  switch (op) {
    case Op::Invalid: return "???";
    case Op::Mov: return "MOV"; case Op::MovFpe: return "MOVFPE"; case Op::MovTpe: return "MOVTPE";
    case Op::Ldm: return "LDM"; case Op::Stm: return "STM"; case Op::Xch: return "XCH"; case Op::Swap: return "SWAP";
    case Op::Add: return "ADD"; case Op::AddQ: return "ADD:Q"; case Op::Adds: return "ADDS"; case Op::Addx: return "ADDX";
    case Op::Dadd: return "DADD"; case Op::Sub: return "SUB"; case Op::Subs: return "SUBS"; case Op::Subx: return "SUBX";
    case Op::Dsub: return "DSUB"; case Op::Mulxu: return "MULXU"; case Op::Divxu: return "DIVXU"; case Op::Cmp: return "CMP";
    case Op::Exts: return "EXTS"; case Op::Extu: return "EXTU"; case Op::Tst: return "TST"; case Op::Neg: return "NEG";
    case Op::Clr: return "CLR"; case Op::Tas: return "TAS";
    case Op::Shal: return "SHAL"; case Op::Shar: return "SHAR"; case Op::Shll: return "SHLL"; case Op::Shlr: return "SHLR";
    case Op::Rotl: return "ROTL"; case Op::Rotr: return "ROTR"; case Op::Rotxl: return "ROTXL"; case Op::Rotxr: return "ROTXR";
    case Op::And: return "AND"; case Op::Or: return "OR"; case Op::Xor: return "XOR"; case Op::Not: return "NOT";
    case Op::Bset: return "BSET"; case Op::Bclr: return "BCLR"; case Op::Btst: return "BTST"; case Op::Bnot: return "BNOT";
    case Op::Ldc: return "LDC"; case Op::Stc: return "STC"; case Op::Andc: return "ANDC"; case Op::Orc: return "ORC";
    case Op::Xorc: return "XORC"; case Op::Trapa: return "TRAPA"; case Op::TrapVs: return "TRAP/VS"; case Op::Rte: return "RTE";
    case Op::Link: return "LINK"; case Op::Unlk: return "UNLK"; case Op::Sleep: return "SLEEP"; case Op::Nop: return "NOP";
    case Op::Bcc: return "Bcc"; case Op::Jmp: return "JMP"; case Op::Bsr: return "BSR"; case Op::Jsr: return "JSR";
    case Op::Rts: return "RTS"; case Op::Rtd: return "RTD"; case Op::Scb: return "SCB"; case Op::Pjmp: return "PJMP";
    case Op::Pjsr: return "PJSR"; case Op::Prts: return "PRTS"; case Op::Prtd: return "PRTD";
    case Op::Count: break;
  }
  return "?";
}

const char* cc_name(Cc cc) {
  static const char* const names[16] = {"BRA", "BRN", "BHI", "BLS", "BCC", "BCS", "BNE", "BEQ",
                                        "BVC", "BVS", "BPL", "BMI", "BGE", "BLT", "BGT", "BLE"};
  return names[u8(cc) & 15];
}

const char* cr_name(u8 cr) {
  switch (cr & 7) {
    case 0: return "SR"; case 1: return "CCR"; case 3: return "BR"; case 4: return "EP"; case 5: return "DP"; case 7: return "TP";
    default: return "CR?";
  }
}

namespace {

std::string hex(u32 v, int digits) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "H'%0*X", digits, v);
  return buf;
}

std::string signed_hex(s32 v, int digits) {
  return v < 0 ? "-" + hex(u32(-v), digits) : hex(u32(v), digits);
}

std::string reg_name(unsigned r) { return "R" + std::to_string(r & 7); }

std::string size_suffix(Size s) { return s == Size::Word ? ".W" : s == Size::Byte ? ".B" : ""; }

std::string imm_text(const DecodedInsn& d) {
  const int digits = (d.size == Size::Word && !d.has(insn_flags::kImm8)) ? 4 : 2;
  return "#" + hex(u32(d.imm) & (digits == 4 ? 0xFFFFu : 0xFFu), digits);
}

std::string ea_text(const DecodedInsn& d, u32 pc_next) {
  switch (d.ea) {
    case EaMode::Reg: return reg_name(d.ea_reg);
    case EaMode::RegInd: return "@" + reg_name(d.ea_reg);
    case EaMode::Disp8: return "@(" + signed_hex(d.ea_ext, 2) + "," + reg_name(d.ea_reg) + ")";
    case EaMode::Disp16: return "@(" + signed_hex(d.ea_ext, 4) + "," + reg_name(d.ea_reg) + ")";
    case EaMode::PreDec: return "@-" + reg_name(d.ea_reg);
    case EaMode::PostInc: return "@" + reg_name(d.ea_reg) + "+";
    case EaMode::Abs8: return "@" + hex(u32(d.ea_ext) & 0xFF, 2) + ":8";
    case EaMode::Abs16: return "@" + hex(u32(d.ea_ext) & 0xFFFF, 4) + ":16";
    case EaMode::Abs24: return "@" + hex(u32(d.ea_ext) & 0xFFFFFF, 6);
    case EaMode::Imm: return imm_text(d);
    case EaMode::PcRel8:
    case EaMode::PcRel16: return hex((pc_next + u32(d.ea_ext)) & 0xFFFF, 4);
    case EaMode::None: break;
  }
  return "";
}

std::string reglist_text(u8 list) {
  std::string s = "(";
  bool first = true;
  for (int i = 0; i < 8; ++i) {
    if (!(list & (1 << i))) continue;
    int j = i;
    while (j + 1 < 8 && (list & (1 << (j + 1)))) ++j;
    if (!first) s += ",";
    first = false;
    s += reg_name(i);
    if (j > i) s += "-" + reg_name(j);
    i = j;
  }
  return s + ")";
}

}  // namespace

std::string disassemble(const DecodedInsn& d, u32 pc_next) {
  using namespace insn_flags;
  const std::string ea = ea_text(d, pc_next);
  const std::string rd = reg_name(d.reg);
  const std::string sz = size_suffix(d.size);

  switch (d.op) {
    case Op::Invalid: return ".DATA.B (invalid)";
    case Op::Nop: case Op::Rte: case Op::Rts: case Op::Sleep: case Op::TrapVs: case Op::Prts:
      return op_name(d.op);
    case Op::Unlk: return "UNLK FP";
    case Op::Link: return "LINK FP,#" + std::to_string(d.imm);
    case Op::Rtd: case Op::Prtd: return std::string(op_name(d.op)) + " #" + std::to_string(d.imm);
    case Op::Trapa: return "TRAPA #" + std::to_string(d.reg);
    case Op::Ldm: return "LDM.W @SP+," + reglist_text(d.reg);
    case Op::Stm: return "STM.W " + reglist_text(d.reg) + ",@-SP";
    case Op::Bcc: return std::string(cc_name(Cc(d.reg))) + " " + ea;
    case Op::Bsr: case Op::Jmp: case Op::Jsr: case Op::Pjmp: case Op::Pjsr:
      return std::string(op_name(d.op)) + " " + ea;
    case Op::Scb: {
      const char* c = d.aux == u8(ScbCond::F) ? "F" : d.aux == u8(ScbCond::NE) ? "NE" : "EQ";
      return std::string("SCB/") + c + " " + rd + "," + ea;
    }
    case Op::Mov: {
      std::string mn = "MOV";
      if (d.has(kShort)) {
        if (d.ea == EaMode::Imm) return (d.size == Size::Word ? "MOV:I " : "MOV:E ") + ea + "," + rd;
        if (d.ea == EaMode::Abs8) mn = d.has(kStore) ? "MOV:S" : "MOV:L";
        else mn = "MOV:F";
      }
      if (d.has(kImmSrc)) return mn + sz + " " + imm_text(d) + "," + ea;
      if (d.has(kStore)) return mn + sz + " " + rd + "," + ea;
      return mn + sz + " " + ea + "," + rd;
    }
    case Op::Cmp:
      if (d.has(kShort)) return (d.size == Size::Word ? "CMP:I " : "CMP:E ") + ea + "," + rd;
      if (d.has(kImmSrc)) return "CMP" + sz + " " + imm_text(d) + "," + ea;
      return "CMP" + sz + " " + ea + "," + rd;
    case Op::AddQ: return "ADD:Q" + sz + " #" + std::to_string(d.imm) + "," + ea;
    case Op::MovFpe: return "MOVFPE.B " + ea + "," + rd;
    case Op::MovTpe: return "MOVTPE.B " + rd + "," + ea;
    case Op::Xch: return "XCH.W " + ea + "," + rd;
    case Op::Dadd: case Op::Dsub: return std::string(op_name(d.op)) + ".B " + ea + "," + rd;
    case Op::Swap: case Op::Exts: case Op::Extu: return std::string(op_name(d.op)) + ".B " + rd;
    case Op::Add: case Op::Adds: case Op::Addx: case Op::Sub: case Op::Subs: case Op::Subx:
    case Op::Mulxu: case Op::Divxu: case Op::And: case Op::Or: case Op::Xor:
      return std::string(op_name(d.op)) + sz + " " + ea + "," + rd;
    case Op::Tst: case Op::Neg: case Op::Clr: case Op::Not:
    case Op::Shal: case Op::Shar: case Op::Shll: case Op::Shlr:
    case Op::Rotl: case Op::Rotr: case Op::Rotxl: case Op::Rotxr:
      return std::string(op_name(d.op)) + sz + " " + ea;
    case Op::Tas: return "TAS.B " + ea;
    case Op::Bset: case Op::Bclr: case Op::Bnot: case Op::Btst: {
      const std::string bit = d.has(kBitInReg) ? rd : "#" + std::to_string(d.aux);
      return std::string(op_name(d.op)) + sz + " " + bit + "," + ea;
    }
    case Op::Ldc: return "LDC" + sz + " " + ea + "," + cr_name(d.reg);
    case Op::Stc: return "STC" + sz + " " + std::string(cr_name(d.reg)) + "," + ea;
    case Op::Andc: case Op::Orc: case Op::Xorc:
      return std::string(op_name(d.op)) + sz + " " + imm_text(d) + "," + cr_name(d.reg);
    case Op::Count: break;
  }
  return "?";
}

std::string format_bytes(const u8* bytes, unsigned length) {
  std::string s;
  char buf[4];
  for (unsigned i = 0; i < length; ++i) {
    std::snprintf(buf, sizeof buf, "%02X", bytes[i]);
    if (i) s += ' ';
    s += buf;
  }
  return s;
}

}  // namespace h8500

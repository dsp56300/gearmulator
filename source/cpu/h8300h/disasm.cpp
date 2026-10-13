#include "cpu/h8300h/disasm.hpp"

#include <cstdio>

namespace h8300h {

namespace {

std::string hex(u32 v, int digits) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "H'%0*X", digits, v);
  return buf;
}

std::string signed_hex(s32 v, int digits) { return v < 0 ? "-" + hex(u32(-v), digits) : hex(u32(v), digits); }

std::string reg(unsigned r, unsigned size) {
  static const char* const b[16] = {"R0H", "R1H", "R2H", "R3H", "R4H", "R5H", "R6H", "R7H",
                                    "R0L", "R1L", "R2L", "R3L", "R4L", "R5L", "R6L", "R7L"};
  static const char* const w[16] = {"R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
                                    "E0", "E1", "E2", "E3", "E4", "E5", "E6", "E7"};
  if (size == 1) return b[r & 15];
  if (size == 2) return w[r & 15];
  return "ER" + std::to_string(r & 7);
}

std::string er(unsigned r) { return "ER" + std::to_string(r & 7); }

const char* suffix(unsigned size) { return size == 1 ? ".B" : size == 2 ? ".W" : ".L"; }

const char* cc_name(unsigned cc) {
  static const char* const names[16] = {"BRA", "BRN", "BHI", "BLS", "BCC", "BCS", "BNE", "BEQ",
                                        "BVC", "BVS", "BPL", "BMI", "BGE", "BLT", "BGT", "BLE"};
  return names[cc & 15];
}

const char* bit_name(BitKind k) {
  switch (k) {
    case BitKind::Bset: return "BSET"; case BitKind::Bnot: return "BNOT"; case BitKind::Bclr: return "BCLR";
    case BitKind::Btst: return "BTST"; case BitKind::Bst: return "BST"; case BitKind::Bist: return "BIST";
    case BitKind::Bor: return "BOR"; case BitKind::Bior: return "BIOR"; case BitKind::Bxor: return "BXOR";
    case BitKind::Bixor: return "BIXOR"; case BitKind::Band: return "BAND"; case BitKind::Biand: return "BIAND";
    case BitKind::Bld: return "BLD"; case BitKind::Bild: return "BILD";
  }
  return "B?";
}

std::string imm_of(const DecodedInsn& d) {
  const unsigned digits = d.x == 1 ? 2 : d.x == 2 ? 4 : 8;
  return "#" + hex(d.x == 1 ? (d.imm & 0xFF) : d.x == 2 ? (d.imm & 0xFFFF) : d.imm, int(digits));
}

std::string abs_of(const DecodedInsn& d) {
  switch (d.op) {
    case Op::MovAbs8: return "@" + hex(d.imm & 0xFF, 2) + ":8";
    case Op::MovAbs16: case Op::LdcAbs16: case Op::StcAbs16: return "@" + hex(d.imm & 0xFFFF, 4) + ":16";
    default: return "@" + hex(d.imm & 0xFFFFFF, 6) + ":24";
  }
}

std::string mem_pair(const DecodedInsn& d, const std::string& mem) {
  const std::string r = reg(d.rd, d.x);
  return d.store ? r + "," + mem : mem + "," + r;
}

}  // namespace

std::string disassemble(const DecodedInsn& d, u32 pc_next) {
  auto rel = [&](u32 disp) { return hex((pc_next + disp) & kAddrMask, 6); };
  switch (d.op) {
    case Op::Invalid: return ".DATA.W ???";
    case Op::Nop: return "NOP";
    case Op::Sleep: return "SLEEP";
    case Op::MovRR: return std::string("MOV") + suffix(d.x) + " " + reg(d.rs, d.x) + "," + reg(d.rd, d.x);
    case Op::MovImm: return std::string("MOV") + suffix(d.x) + " " + imm_of(d) + "," + reg(d.rd, d.x);
    case Op::MovAbs8: case Op::MovAbs16: case Op::MovAbs24: return std::string("MOV") + suffix(d.x) + " " + mem_pair(d, abs_of(d));
    case Op::MovInd: return std::string("MOV") + suffix(d.x) + " " + mem_pair(d, "@" + er(d.rs));
    case Op::MovDisp16: return std::string("MOV") + suffix(d.x) + " " + mem_pair(d, "@(" + signed_hex(s32(d.imm), 4) + ":16," + er(d.rs) + ")");
    case Op::MovDisp24: return std::string("MOV") + suffix(d.x) + " " + mem_pair(d, "@(" + hex(d.imm, 8) + ":24," + er(d.rs) + ")");
    case Op::MovPost: return std::string("MOV") + suffix(d.x) + " " + (d.store ? reg(d.rd, d.x) + ",@-" + er(d.rs) : "@" + er(d.rs) + "+," + reg(d.rd, d.x));
    case Op::Movfpe: return "MOVFPE @" + hex(d.imm & 0xFFFF, 4) + ":16," + reg(d.rd, 1);
    case Op::Movtpe: return "MOVTPE " + reg(d.rd, 1) + ",@" + hex(d.imm & 0xFFFF, 4) + ":16";
    case Op::Add: return std::string("ADD") + suffix(d.x) + " " + reg(d.rs, d.x) + "," + reg(d.rd, d.x);
    case Op::Sub: return std::string("SUB") + suffix(d.x) + " " + reg(d.rs, d.x) + "," + reg(d.rd, d.x);
    case Op::Cmp: return std::string("CMP") + suffix(d.x) + " " + reg(d.rs, d.x) + "," + reg(d.rd, d.x);
    case Op::And: return std::string("AND") + suffix(d.x) + " " + reg(d.rs, d.x) + "," + reg(d.rd, d.x);
    case Op::Or: return std::string("OR") + suffix(d.x) + " " + reg(d.rs, d.x) + "," + reg(d.rd, d.x);
    case Op::Xor: return std::string("XOR") + suffix(d.x) + " " + reg(d.rs, d.x) + "," + reg(d.rd, d.x);
    case Op::AddImm: return std::string("ADD") + suffix(d.x) + " " + imm_of(d) + "," + reg(d.rd, d.x);
    case Op::SubImm: return std::string("SUB") + suffix(d.x) + " " + imm_of(d) + "," + reg(d.rd, d.x);
    case Op::CmpImm: return std::string("CMP") + suffix(d.x) + " " + imm_of(d) + "," + reg(d.rd, d.x);
    case Op::AndImm: return std::string("AND") + suffix(d.x) + " " + imm_of(d) + "," + reg(d.rd, d.x);
    case Op::OrImm: return std::string("OR") + suffix(d.x) + " " + imm_of(d) + "," + reg(d.rd, d.x);
    case Op::XorImm: return std::string("XOR") + suffix(d.x) + " " + imm_of(d) + "," + reg(d.rd, d.x);
    case Op::Addx: return "ADDX " + reg(d.rs, 1) + "," + reg(d.rd, 1);
    case Op::Subx: return "SUBX " + reg(d.rs, 1) + "," + reg(d.rd, 1);
    case Op::AddxImm: return "ADDX #" + hex(d.imm & 0xFF, 2) + "," + reg(d.rd, 1);
    case Op::SubxImm: return "SUBX #" + hex(d.imm & 0xFF, 2) + "," + reg(d.rd, 1);
    case Op::Inc: return std::string("INC") + suffix(d.x) + " " + (d.x == 1 ? "" : "#" + std::to_string(d.imm) + ",") + reg(d.rd, d.x);
    case Op::Dec: return std::string("DEC") + suffix(d.x) + " " + (d.x == 1 ? "" : "#" + std::to_string(d.imm) + ",") + reg(d.rd, d.x);
    case Op::Adds: return "ADDS #" + std::to_string(d.imm) + "," + er(d.rd);
    case Op::Subs: return "SUBS #" + std::to_string(d.imm) + "," + er(d.rd);
    case Op::Mulxu: return std::string("MULXU") + suffix(d.x == 2 ? 1 : 2) + " " + reg(d.rs, d.x == 2 ? 1 : 2) + "," + reg(d.rd, d.x);
    case Op::Mulxs: return std::string("MULXS") + suffix(d.x == 2 ? 1 : 2) + " " + reg(d.rs, d.x == 2 ? 1 : 2) + "," + reg(d.rd, d.x);
    case Op::Divxu: return std::string("DIVXU") + suffix(d.x == 2 ? 1 : 2) + " " + reg(d.rs, d.x == 2 ? 1 : 2) + "," + reg(d.rd, d.x);
    case Op::Divxs: return std::string("DIVXS") + suffix(d.x == 2 ? 1 : 2) + " " + reg(d.rs, d.x == 2 ? 1 : 2) + "," + reg(d.rd, d.x);
    case Op::Neg: return std::string("NEG") + suffix(d.x) + " " + reg(d.rd, d.x);
    case Op::Not: return std::string("NOT") + suffix(d.x) + " " + reg(d.rd, d.x);
    case Op::Extu: return std::string("EXTU") + suffix(d.x) + " " + reg(d.rd, d.x);
    case Op::Exts: return std::string("EXTS") + suffix(d.x) + " " + reg(d.rd, d.x);
    case Op::Shal: return std::string("SHAL") + suffix(d.x) + " " + reg(d.rd, d.x);
    case Op::Shar: return std::string("SHAR") + suffix(d.x) + " " + reg(d.rd, d.x);
    case Op::Shll: return std::string("SHLL") + suffix(d.x) + " " + reg(d.rd, d.x);
    case Op::Shlr: return std::string("SHLR") + suffix(d.x) + " " + reg(d.rd, d.x);
    case Op::Rotl: return std::string("ROTL") + suffix(d.x) + " " + reg(d.rd, d.x);
    case Op::Rotr: return std::string("ROTR") + suffix(d.x) + " " + reg(d.rd, d.x);
    case Op::Rotxl: return std::string("ROTXL") + suffix(d.x) + " " + reg(d.rd, d.x);
    case Op::Rotxr: return std::string("ROTXR") + suffix(d.x) + " " + reg(d.rd, d.x);
    case Op::BitReg: case Op::BitInd: case Op::BitAbs: {
      const BitKind k = BitKind(d.x & 15);
      const std::string bit = (d.x & 0x80) ? reg(d.rs, 1) : "#" + std::to_string((d.x >> 4) & 7);
      std::string operand;
      if (d.op == Op::BitReg) operand = reg(d.rd, 1);
      else if (d.op == Op::BitInd) operand = "@" + er(d.rd);
      else operand = (d.imm & 0xFFFFFF00u) == 0xFFFF00u && d.len == 4 ? "@" + hex(d.imm & 0xFF, 2) + ":8" : d.len == 6 ? "@" + hex(d.imm & 0xFFFF, 4) + ":16" : "@" + hex(d.imm & 0xFFFFFF, 6) + ":24";
      return std::string(bit_name(k)) + " " + bit + "," + operand;
    }
    case Op::Bcc8: return std::string(cc_name(d.x)) + " " + rel(d.imm) + ":8";
    case Op::Bcc16: return std::string(cc_name(d.x)) + " " + rel(d.imm) + ":16";
    case Op::JmpEr: return "JMP @" + er(d.rd);
    case Op::JmpAbs: return "JMP @" + hex(d.imm & 0xFFFFFF, 6) + ":24";
    case Op::JmpInd8: return "JMP @@" + hex(d.imm & 0xFF, 2) + ":8";
    case Op::JsrEr: return "JSR @" + er(d.rd);
    case Op::JsrAbs: return "JSR @" + hex(d.imm & 0xFFFFFF, 6) + ":24";
    case Op::JsrInd8: return "JSR @@" + hex(d.imm & 0xFF, 2) + ":8";
    case Op::Bsr8: return "BSR " + rel(d.imm) + ":8";
    case Op::Bsr16: return "BSR " + rel(d.imm) + ":16";
    case Op::Rts: return "RTS";
    case Op::Rte: return "RTE";
    case Op::Trapa: return "TRAPA #" + std::to_string(d.imm & 3);
    case Op::LdcImm: return "LDC #" + hex(d.imm & 0xFF, 2) + ",CCR";
    case Op::LdcReg: return "LDC " + reg(d.rd, 1) + ",CCR";
    case Op::StcReg: return "STC CCR," + reg(d.rd, 1);
    case Op::LdcAbs16: case Op::LdcAbs24: return "LDC " + abs_of(d) + ",CCR";
    case Op::StcAbs16: case Op::StcAbs24: return "STC CCR," + abs_of(d);
    case Op::LdcInd: return "LDC @" + er(d.rs) + ",CCR";
    case Op::StcInd: return "STC CCR,@" + er(d.rs);
    case Op::LdcDisp16: return "LDC @(" + signed_hex(s32(d.imm), 4) + ":16," + er(d.rs) + "),CCR";
    case Op::StcDisp16: return "STC CCR,@(" + signed_hex(s32(d.imm), 4) + ":16," + er(d.rs) + ")";
    case Op::LdcDisp24: return "LDC @(" + hex(d.imm, 8) + ":24," + er(d.rs) + "),CCR";
    case Op::StcDisp24: return "STC CCR,@(" + hex(d.imm, 8) + ":24," + er(d.rs) + ")";
    case Op::LdcPost: return "LDC @" + er(d.rs) + "+,CCR";
    case Op::StcPre: return "STC CCR,@-" + er(d.rs);
    case Op::Andc: return "ANDC #" + hex(d.imm & 0xFF, 2) + ",CCR";
    case Op::Orc: return "ORC #" + hex(d.imm & 0xFF, 2) + ",CCR";
    case Op::Xorc: return "XORC #" + hex(d.imm & 0xFF, 2) + ",CCR";
    case Op::Daa: return "DAA " + reg(d.rd, 1);
    case Op::Das: return "DAS " + reg(d.rd, 1);
    case Op::Eepmov: return d.x == 1 ? "EEPMOV.B" : "EEPMOV.W";
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

}  // namespace h8300h

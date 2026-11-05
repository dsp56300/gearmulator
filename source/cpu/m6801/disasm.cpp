#include "cpu/m6801/disasm.hpp"

#include <cstdio>

namespace m6801 {

namespace {

std::string hex(u32 v, int digits) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "$%0*X", digits, v);
  return buf;
}

const char* name(Op op) {
  switch (op) {
    case Op::Illegal: return "???";
    case Op::Trap: return "TRAP";
    case Op::Nop: return "NOP";
    case Op::Lsrd: return "LSRD";
    case Op::Asld: return "ASLD";
    case Op::Tap: return "TAP";
    case Op::Tpa: return "TPA";
    case Op::Inx: return "INX";
    case Op::Dex: return "DEX";
    case Op::Clv: return "CLV";
    case Op::Sev: return "SEV";
    case Op::Clc: return "CLC";
    case Op::Sec: return "SEC";
    case Op::Cli: return "CLI";
    case Op::Sei: return "SEI";
    case Op::Sba: return "SBA";
    case Op::Cba: return "CBA";
    case Op::Undoc: return "ADX";  // undocumented: X += (S+1)
    case Op::Tab: return "TAB";
    case Op::Tba: return "TBA";
    case Op::Xgdx: return "XGDX";
    case Op::Daa: return "DAA";
    case Op::Slp: return "SLP";
    case Op::Aba: return "ABA";
    case Op::Bcc: return "B";
    case Op::Tsx: return "TSX";
    case Op::Ins: return "INS";
    case Op::Pul: return "PUL";
    case Op::Des: return "DES";
    case Op::Txs: return "TXS";
    case Op::Psh: return "PSH";
    case Op::Pulx: return "PULX";
    case Op::Rts: return "RTS";
    case Op::Abx: return "ABX";
    case Op::Rti: return "RTI";
    case Op::Pshx: return "PSHX";
    case Op::Mul: return "MUL";
    case Op::Wai: return "WAI";
    case Op::Swi: return "SWI";
    case Op::Neg: return "NEG";
    case Op::Com: return "COM";
    case Op::Lsr: return "LSR";
    case Op::Ror: return "ROR";
    case Op::Asr: return "ASR";
    case Op::Asl: return "ASL";
    case Op::Rol: return "ROL";
    case Op::Dec: return "DEC";
    case Op::Inc: return "INC";
    case Op::Tst: return "TST";
    case Op::Clr: return "CLR";
    case Op::Aim: return "AIM";
    case Op::Oim: return "OIM";
    case Op::Eim: return "EIM";
    case Op::Tim: return "TIM";
    case Op::Jmp: return "JMP";
    case Op::Jsr: return "JSR";
    case Op::Bsr: return "BSR";
    case Op::Sub: return "SUB";
    case Op::Cmp: return "CMP";
    case Op::Sbc: return "SBC";
    case Op::And: return "AND";
    case Op::Bit: return "BIT";
    case Op::Lda: return "LDA";
    case Op::Sta: return "STA";
    case Op::Eor: return "EOR";
    case Op::Adc: return "ADC";
    case Op::Ora: return "ORA";
    case Op::Add: return "ADD";
    case Op::Subd: return "SUBD";
    case Op::Addd: return "ADDD";
    case Op::Cpx: return "CPX";
    case Op::Ldd: return "LDD";
    case Op::Ldx: return "LDX";
    case Op::Lds: return "LDS";
    case Op::Std: return "STD";
    case Op::Stx: return "STX";
    case Op::Sts: return "STS";
    case Op::Cpd: return "CPD";
    case Op::Idiv: return "IDIV";
    case Op::Fdiv: return "FDIV";
    case Op::Bset: return "BSET";
    case Op::Bclr: return "BCLR";
    case Op::Brset: return "BRSET";
    case Op::Brclr: return "BRCLR";
    case Op::Stop: return "STOP";
  }
  return "???";
}

// The X instructions, whose Y forms (HC11) end in Y instead.
bool index_register_op(Op op) {
  switch (op) {
    case Op::Inx: case Op::Dex: case Op::Tsx: case Op::Txs: case Op::Abx: case Op::Pshx: case Op::Pulx: case Op::Xgdx:
    case Op::Cpx: case Op::Ldx: case Op::Stx:
      return true;
    default: return false;
  }
}

bool takes_register(Op op) {
  switch (op) {
    case Op::Pul: case Op::Psh:
    case Op::Sub: case Op::Cmp: case Op::Sbc: case Op::And: case Op::Bit: case Op::Lda: case Op::Sta: case Op::Eor:
    case Op::Adc: case Op::Ora: case Op::Add:
      return true;
    default: return false;
  }
}

bool sixteen_bit(Op op) {
  switch (op) {
    case Op::Subd: case Op::Addd: case Op::Cpx: case Op::Ldd: case Op::Ldx: case Op::Lds: case Op::Std: case Op::Stx:
    case Op::Sts: case Op::Cpd:
      return true;
    default: return false;
  }
}

}  // namespace

std::string disassemble(const DecodedInsn& d) {
  static const char* const cc[16] = {"RA", "RN", "HI", "LS", "CC", "CS", "NE", "EQ",
                                     "VC", "VS", "PL", "MI", "GE", "LT", "GT", "LE"};
  std::string s = name(d.op);
  if (d.op == Op::Bcc) s += cc[d.cond & 15];
  if (index_register_op(d.op) && d.reg) s.back() = 'Y';
  // Accumulator forms (LDAA, PSHB, NEGA); the unary operations on memory have none.
  const bool unary_acc = d.mode == Mode::Inh && d.op >= Op::Neg && d.op <= Op::Clr;
  if (takes_register(d.op) || unary_acc) s += d.reg ? "B" : "A";
  switch (d.mode) {
    case Mode::Inh: return d.op == Op::Illegal ? s + " " + hex(d.opcode, 2) : s;
    case Mode::Imm: return s + " #" + hex(d.imm, sixteen_bit(d.op) ? 4 : 2);
    case Mode::Rel: return s + " " + hex(d.imm, 4);
    default: break;
  }
  const bool bitop = d.op == Op::Aim || d.op == Op::Oim || d.op == Op::Eim || d.op == Op::Tim;
  // HC11 BSET / BCLR: operand, mask; BRSET / BRCLR: operand, mask, target.
  const bool hc11_bit = d.op == Op::Bset || d.op == Op::Bclr || d.op == Op::Brset || d.op == Op::Brclr;
  const bool branch = d.op == Op::Brset || d.op == Op::Brclr;
  const u16 where = branch ? d.ea : d.imm;
  std::string operand = bitop ? "#" + hex(d.x, 2) + "," : std::string();
  switch (d.mode) {
    case Mode::Dir: operand += hex(where, 2); break;
    case Mode::Idx: operand += hex(where, 2) + ",X"; break;
    case Mode::Idy: operand += hex(where, 2) + ",Y"; break;
    default: operand += hex(where, 4); break;
  }
  if (hc11_bit) operand += ",#" + hex(d.x, 2);
  if (branch) operand += "," + hex(d.imm, 4);
  return s + " " + operand;
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

}  // namespace m6801

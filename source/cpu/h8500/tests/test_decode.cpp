// Decoder tests: encodings taken from the H8/500 programming manual
// (instruction format boxes, Table 2-1 / A-1) and the H8/510 hardware manual.
#include <type_traits>
#include <vector>

#include "cpu/h8500/disasm.hpp"
#include "common/test_util.hpp"

using namespace h8500;

namespace {

struct Case {
  std::vector<u8> bytes;
  const char* text;   // expected disassembly (pc_next = 0x1000 + length for PC-relative)
  unsigned length;
};

void run_cases() {
  const Case cases[] = {
      // --- manual examples ---
      {{0xD0, 0x21}, "ADD.B @R0,R1", 2},                 // A.2 example 1
      {{0x0D, 0x11, 0x21}, "ADD.W @H'11:8,R1", 3},       // A.2 example 2
      {{0x11, 0xD8}, "JSR @R0", 2},                      // 2.6.5 example 2
      {{0x00}, "NOP", 1},
      // --- data transfer ---
      {{0xA1, 0x80}, "MOV.B R1,R0", 2},
      {{0xD9, 0x82}, "MOV.W @R1,R2", 2},
      {{0xD9, 0x92}, "MOV.W R2,@R1", 2},
      {{0xE0, 0x10, 0x83}, "MOV.B @(H'10,R0),R3", 3},
      {{0xF8, 0xFF, 0xFE, 0x93}, "MOV.W R3,@(-H'0002,R0)", 4},
      {{0xB7, 0x90}, "MOV.B R0,@-R7", 2},
      {{0xCF, 0x85}, "MOV.W @R7+,R5", 2},
      {{0x05, 0xA0, 0x80}, "MOV.B @H'A0:8,R0", 3},
      {{0x1D, 0x12, 0x34, 0x97}, "MOV.W R7,@H'1234:16", 4},
      {{0x04, 0x55, 0x80}, "MOV.B #H'55,R0", 3},
      {{0x0C, 0xFF, 0x00, 0x85}, "MOV.W #H'FF00,R5", 4},
      {{0xDC, 0x07, 0x12, 0x34}, "MOV.W #H'1234,@R4", 4},
      {{0xDC, 0x06, 0x80}, "MOV.W #H'80,@R4", 3},
      {{0x1D, 0x12, 0x34, 0x07, 0x56, 0x78}, "MOV.W #H'5678,@H'1234:16", 6},
      {{0x50, 0x55}, "MOV:E #H'55,R0", 2},
      {{0x5D, 0xFF, 0x00}, "MOV:I #H'FF00,R5", 3},
      {{0x60, 0xA0}, "MOV:L.B @H'A0:8,R0", 2},
      {{0x78, 0xA0}, "MOV:S.W R0,@H'A0:8", 2},
      {{0x80, 0x04}, "MOV:F.B @(H'04,R6),R0", 2},
      {{0x98, 0xFE}, "MOV:F.W R0,@(-H'02,R6)", 2},
      {{0xD0, 0x00, 0x81}, "MOVFPE.B @R0,R1", 3},
      {{0xD1, 0x00, 0x90}, "MOVTPE.B R0,@R1", 3},
      {{0x02, 0x15}, "LDM.W @SP+,(R0,R2,R4)", 2},
      {{0x12, 0x0F}, "STM.W (R0-R3),@-SP", 2},
      {{0x12, 0x8F}, "STM.W (R0-R3,R7),@-SP", 2},
      {{0xA9, 0x90}, "XCH.W R1,R0", 2},
      {{0xA4, 0x10}, "SWAP.B R4", 2},
      // --- arithmetic ---
      {{0x04, 0x05, 0x20}, "ADD.B #H'05,R0", 3},
      {{0xD0, 0x08}, "ADD:Q.B #1,@R0", 2},
      {{0xA8, 0x0D}, "ADD:Q.W #-2,R0", 2},
      {{0x0C, 0x00, 0x10, 0x2B}, "ADDS.W #H'0010,R3", 4},
      {{0xE4, 0x20, 0xA0}, "ADDX.B @(H'20,R4),R0", 3},
      {{0xA0, 0x00, 0xA1}, "DADD.B R0,R1", 3},
      {{0xD9, 0x30}, "SUB.W @R1,R0", 2},
      {{0x04, 0x02, 0x3A}, "SUBS.B #H'02,R2", 3},
      {{0xCA, 0xB0}, "SUBX.W @R2+,R0", 2},
      {{0xA2, 0x00, 0xB3}, "DSUB.B R2,R3", 3},
      {{0xA0, 0xA9}, "MULXU.B R0,R1", 2},
      {{0xDB, 0xB8}, "DIVXU.W @R3,R0", 2},
      {{0xB3, 0x04, 0xAA}, "CMP.B #H'AA,@-R3", 3},
      {{0xDB, 0x05, 0x12, 0x34}, "CMP.W #H'1234,@R3", 4},
      {{0x40, 0x00}, "CMP:E #H'00,R0", 2},
      {{0x49, 0xFF, 0xFF}, "CMP:I #H'FFFF,R1", 3},
      {{0xA0, 0x11}, "EXTS.B R0", 2},
      {{0xA1, 0x12}, "EXTU.B R1", 2},
      {{0xF9, 0x10, 0x00, 0x16}, "TST.W @(H'1000,R1)", 4},
      {{0xA8, 0x14}, "NEG.W R0", 2},
      {{0xC8, 0x13}, "CLR.W @R0+", 2},
      {{0x15, 0xF0, 0x00, 0x17}, "TAS.B @H'F000:16", 4},
      // --- shifts / logic ---
      {{0xB7, 0x1A}, "SHLL.B @-R7", 2},
      {{0xC2, 0x18}, "SHAL.B @R2+", 2},
      {{0xA8, 0x1C}, "ROTL.W R0", 2},
      {{0xE9, 0x02, 0x1E}, "ROTXL.W @(H'02,R1)", 3},
      {{0x05, 0xF8, 0x51}, "AND.B @H'F8:8,R1", 3},
      {{0x05, 0xF0, 0x41}, "OR.B @H'F0:8,R1", 3},
      {{0x05, 0xA0, 0x60}, "XOR.B @H'A0:8,R0", 3},
      {{0xE2, 0x10, 0x15}, "NOT.B @(H'10,R2)", 3},
      // --- bit manipulation ---
      {{0x15, 0xFF, 0x00, 0xC3}, "BSET.B #3,@H'FF00:16", 4},
      {{0x15, 0xFF, 0x00, 0xD7}, "BCLR.B #7,@H'FF00:16", 4},
      {{0xA9, 0x68}, "BNOT.W R0,R1", 2},
      {{0x05, 0xF0, 0x78}, "BTST.B R0,@H'F0:8", 3},
      {{0xC1, 0x48}, "BSET.B R0,@R1+", 2},
      // --- system control ---
      {{0x04, 0xFE, 0x59}, "ANDC.B #H'FE,CCR", 3},
      {{0x0C, 0x07, 0x00, 0x48}, "ORC.W #H'0700,SR", 4},
      {{0x04, 0x01, 0x69}, "XORC.B #H'01,CCR", 3},
      {{0x04, 0x01, 0x8D}, "LDC.B #H'01,DP", 3},
      {{0xA1, 0x8F}, "LDC.B R1,TP", 2},
      {{0xD8, 0x98}, "STC.W SR,@R0", 2},
      {{0xA0, 0x9B}, "STC.B BR,R0", 2},
      {{0x08, 0x14}, "TRAPA #4", 2},
      {{0x09}, "TRAP/VS", 1},
      {{0x0A}, "RTE", 1},
      {{0x17, 0xFC}, "LINK FP,#-4", 2},
      {{0x1F, 0xFF, 0x00}, "LINK FP,#-256", 3},
      {{0x0F}, "UNLK FP", 1},
      {{0x1A}, "SLEEP", 1},
      // --- branches (pc_next = 0x1000 + length) ---
      {{0x20, 0x10}, "BRA H'1012", 2},
      {{0x27, 0xFE}, "BEQ H'1000", 2},
      {{0x37, 0xFF, 0x00}, "BEQ H'0F03", 3},
      {{0x2F, 0x7F}, "BLE H'1081", 2},
      {{0x10, 0x12, 0x34}, "JMP @H'1234:16", 3},
      {{0x11, 0xD3}, "JMP @R3", 2},
      {{0x11, 0xE4, 0x10}, "JMP @(H'10,R4)", 3},
      {{0x11, 0xF4, 0x01, 0x00}, "JMP @(H'0100,R4)", 4},
      {{0x0E, 0x10}, "BSR H'1012", 2},
      {{0x1E, 0x01, 0x00}, "BSR H'1103", 3},
      {{0x18, 0x12, 0x34}, "JSR @H'1234:16", 3},
      {{0x11, 0xEB, 0xF0}, "JSR @(-H'10,R3)", 3},
      {{0x11, 0xF8, 0x0F, 0xFF}, "JSR @(H'0FFF,R0)", 4},
      {{0x19}, "RTS", 1},
      {{0x14, 0x02}, "RTD #2", 2},
      {{0x1C, 0x01, 0x00}, "RTD #256", 3},
      {{0x01, 0xB9, 0xFE}, "SCB/F R1,H'1001", 3},
      {{0x06, 0xBB, 0xFA}, "SCB/NE R3,H'0FFD", 3},
      {{0x07, 0xBC, 0x00}, "SCB/EQ R4,H'1003", 3},
      {{0x13, 0x12, 0x34, 0x56}, "PJMP @H'123456", 4},
      {{0x11, 0xC2}, "PJMP @R2", 2},
      {{0x03, 0x01, 0x00, 0x00}, "PJSR @H'010000", 4},
      {{0x11, 0xCA}, "PJSR @R2", 2},
      {{0x11, 0x19}, "PRTS", 2},
      {{0x11, 0x14, 0x08}, "PRTD #8", 3},
      {{0x11, 0x1C, 0x01, 0x00}, "PRTD #256", 4},
  };

  for (const Case& c : cases) {
    u8 buf[8] = {};
    for (size_t i = 0; i < c.bytes.size(); ++i) buf[i] = c.bytes[i];
    const DecodedInsn d = decode(buf);
    const std::string text = disassemble(d, 0x1000 + d.length);
    if (text != c.text || d.length != c.length) {
      ++test::g_failures;
      std::printf("FAIL decode [%s]: got \"%s\" (len %u), expected \"%s\" (len %u)\n",
                  format_bytes(buf, c.bytes.size()).c_str(), text.c_str(), d.length, c.text, c.length);
    }
    ++test::g_checks;
  }
}

void run_field_checks() {
  using namespace insn_flags;
  // ADD.B @R0,R1
  DecodedInsn d = decode(std::vector<u8>{0xD0, 0x21}.data());
  CHECK_EQ(d.op, Op::Add);
  CHECK_EQ(d.size, Size::Byte);
  CHECK_EQ(d.ea, EaMode::RegInd);
  CHECK_EQ(d.ea_reg, 0);
  CHECK_EQ(d.reg, 1);
  CHECK_EQ(d.length, 2);

  // MOV.W #H'80,@R4  -> 8-bit immediate sign-extended for a word destination
  d = decode(std::vector<u8>{0xDC, 0x06, 0x80}.data());
  CHECK_EQ(d.op, Op::Mov);
  CHECK(d.has(kImmSrc));
  CHECK(d.has(kImm8));
  CHECK_EQ(d.imm, -128);

  // XCH R1,R0: Rs in EA field, Rd in OP field
  d = decode(std::vector<u8>{0xA9, 0x90}.data());
  CHECK_EQ(d.op, Op::Xch);
  CHECK_EQ(d.ea_reg, 1);
  CHECK_EQ(d.reg, 0);

  // PJMP flags
  d = decode(std::vector<u8>{0x13, 0x12, 0x34, 0x56}.data());
  CHECK(d.has(kMaxModeOnly));
  CHECK_EQ(d.ea_ext, 0x123456);

  // Bcc condition field
  d = decode(std::vector<u8>{0x2F, 0x7F}.data());
  CHECK_EQ(Cc(d.reg), Cc::LE);
  CHECK_EQ(d.ea_ext, 127);

  // TRAPA vector
  d = decode(std::vector<u8>{0x08, 0x1F}.data());
  CHECK_EQ(d.reg, 15);

  // ORC.W #H'0700,SR -> cr = SR (0)
  d = decode(std::vector<u8>{0x0C, 0x07, 0x00, 0x48}.data());
  CHECK_EQ(d.op, Op::Orc);
  CHECK_EQ(d.reg, 0);
  CHECK_EQ(d.imm, 0x0700);
}

void run_invalid_checks() {
  const std::vector<std::vector<u8>> invalid = {
      {0x0B}, {0x16}, {0x1B},                    // undefined special opcodes
      {0x11, 0x00}, {0x11, 0xBF},                // 0x11 with undefined second byte
      {0x01, 0x00, 0x00},                        // SCB with bad register byte
      {0xA0, 0x01}, {0xA0, 0x0A}, {0xA0, 0x0F},  // register EA with undefined op
      {0xD0, 0x01}, {0xD0, 0x10}, {0xD0, 0x12},  // memory EA: SWAP/EXTU need a register
      {0x04, 0x00, 0x06},                        // MOV #xx,<EAd> with immediate EA
      {0x04, 0x00, 0x78},                        // BTST with immediate destination
      {0x04, 0x00, 0x90}, {0x04, 0x00, 0xC0},    // XCH/MOV store, BSET #n on immediate
      {0xA0, 0x00, 0x80},                        // MOVFPE needs memory EA
      {0xD0, 0x00, 0xA1},                        // DADD needs register EA
      {0xD0, 0x00, 0x00},                        // prefix followed by undefined code
  };
  for (const auto& b : invalid) {
    u8 buf[8] = {};
    for (size_t i = 0; i < b.size(); ++i) buf[i] = b[i];
    const DecodedInsn d = decode(buf);
    ++test::g_checks;
    if (d.valid()) {
      ++test::g_failures;
      std::printf("FAIL: [%s] decoded as \"%s\" but should be invalid\n", format_bytes(buf, b.size()).c_str(),
                  disassemble(d).c_str());
    }
  }
}

// Every 2-byte general-format combination must decode to a length <= 6 and never
// read beyond the declared length.
void run_exhaustive_lengths() {
  for (unsigned b0 = 0; b0 < 256; ++b0) {
    for (unsigned b1 = 0; b1 < 256; ++b1) {
      unsigned max_read = 0;
      u8 bytes[8] = {u8(b0), u8(b1), 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      const DecodedInsn d = decode([&](unsigned off) {
        if (off + 1 > max_read) max_read = off + 1;
        return bytes[off < 8 ? off : 7];
      });
      ++test::g_checks;
      if (d.length == 0 || d.length > 6 || max_read > d.length) {
        ++test::g_failures;
        std::printf("FAIL: bytes %02X %02X -> length %u, max read %u\n", b0, b1, d.length, max_read);
      }
    }
  }
}

}  // namespace

int main() {
  run_cases();
  run_field_checks();
  run_invalid_checks();
  run_exhaustive_lengths();
  return test::finish("test_decode");
}

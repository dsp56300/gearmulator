// MC6801 / HD6301 / MC68HC11 firmware disassembler.
//
// Walks the code from the vectors (and any --entry addresses), following
// branches, jumps and calls with a direct target; everything it reaches is
// printed as instructions, the rest as data with its ASCII. Indirect jumps
// (JMP 0,X and friends) end a path, so jump tables need --entry.
//
//   m6801dasm [--hd6301 | --hc11] [--org <addr>] [--entry <addr>]... <rom.bin>
//
// --hc11 names the register block at $1000 (its reset position).
//
// The image ends at $FFFF unless --org places it.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "cpu/m6801/disasm.hpp"

using namespace m6801;

namespace {

const char* register_name(u16 a) {
  static const char* const names[0x20] = {
      "P1DDR", "P2DDR", "P1DR", "P2DR", "P3DDR", "P4DDR", "P3DR", "P4DR", "TCSR", "FRCH", "FRCL",
      "OCRH",  "OCRL",  "ICRH", "ICRL", "P3CSR", "RMCR",  "TRCSR", "RDR", "TDR",  "RAMCR"};
  return a < 0x15 ? names[a] : nullptr;
}

const char* hc11_register_name(u16 a) {
  static const char* const names[0x40] = {
      "PORTA", nullptr, "PIOC", "PORTC", "PORTB", "PORTCL", nullptr, "DDRC", "PORTD", "DDRD", "PORTE", "CFORC",
      "OC1M", "OC1D", "TCNT", "TCNT+1", "TIC1", "TIC1+1", "TIC2", "TIC2+1", "TIC3", "TIC3+1", "TOC1", "TOC1+1",
      "TOC2", "TOC2+1", "TOC3", "TOC3+1", "TOC4", "TOC4+1", "TOC5", "TOC5+1", "TCTL1", "TCTL2", "TMSK1", "TFLG1",
      "TMSK2", "TFLG2", "PACTL", "PACNT", "SPCR", "SPSR", "SPDR", "BAUD", "SCCR1", "SCCR2", "SCSR", "SCDR",
      "ADCTL", "ADR1", "ADR2", "ADR3", "ADR4", nullptr, nullptr, nullptr, nullptr, "OPTION", "COPRST", "PPROG",
      "HPRIO", "INIT", "TEST1", "CONFIG"};
  return a >= 0x1000 && a < 0x1040 ? names[a - 0x1000] : nullptr;
}

bool ends_path(Op op) {
  return op == Op::Jmp || op == Op::Rts || op == Op::Rti || op == Op::Illegal || op == Op::Trap ||
         op == Op::Swi;
}

}  // namespace

int main(int argc, char** argv) {
  Family family = Family::M6801;
  long org = -1;
  std::vector<u16> entries;
  const char* path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--hd6301")) family = Family::HD6301;
    else if (!std::strcmp(argv[i], "--hc11")) family = Family::HC11;
    else if (!std::strcmp(argv[i], "--org") && i + 1 < argc) org = std::strtol(argv[++i], nullptr, 0);
    else if (!std::strcmp(argv[i], "--entry") && i + 1 < argc) entries.push_back(u16(std::strtol(argv[++i], nullptr, 0)));
    else path = argv[i];
  }
  if (!path) {
    std::fprintf(stderr, "usage: m6801dasm [--hd6301 | --hc11] [--org <addr>] [--entry <addr>]... <rom.bin>\n");
    return 1;
  }
  std::ifstream f(path, std::ios::binary);
  std::vector<u8> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (rom.empty() || rom.size() > 0x10000) {
    std::fprintf(stderr, "m6801dasm: cannot use %s\n", path);
    return 1;
  }
  const u32 base = org >= 0 ? u32(org) : u32(0x10000 - rom.size());
  std::vector<u8> mem(0x10000, 0xFF);
  for (size_t i = 0; i < rom.size(); ++i) mem[(base + i) & 0xFFFF] = rom[i];
  // A ROM smaller than the top of the space is mirrored up to $FFFF (vectors).
  if (base + rom.size() < 0x10000)
    for (u32 a = base + u32(rom.size()); a < 0x10000; ++a) mem[a] = rom[(a - base) % rom.size()];
  auto in_rom = [&](u32 a) { return a >= base && a < base + rom.size(); };
  auto fetch_at = [&](u16 pc) { return [&mem, pc](unsigned i) { return mem[u16(pc + i)]; }; };

  std::vector<u8> code(0x10000, 0);
  std::set<u16> labels;
  std::vector<u16> work;
  auto add = [&](u16 a, bool label) {
    if (label) labels.insert(a);
    work.push_back(a);
  };
  static const char* const vector_names[] = {"SCI", "TOF", "OCF", "ICF", "IRQ1", "SWI", "NMI", "RESET"};
  static const char* const hc11_vector_names[] = {"SCI", "SPI", "PAI", "PAOV", "TOF", "OC5", "OC4",
                                                  "OC3", "OC2", "OC1", "IC3", "IC2", "IC1", "RTI",
                                                  "IRQ", "XIRQ", "SWI", "ILLOP", "COP", "CME", "RESET"};
  const bool hc11 = family == Family::HC11;
  const unsigned vectors = hc11 ? 21 : 8;
  const u16 vector_base = hc11 ? 0xFFD6 : 0xFFF0;
  for (unsigned v = 0; v < vectors; ++v) {
    const u16 at = u16(vector_base + 2 * v);
    add(u16((mem[at] << 8) | mem[at + 1]), true);
  }
  if (family == Family::HD6301) add(u16((mem[0xFFEE] << 8) | mem[0xFFEF]), true);
  for (u16 e : entries) add(e, true);

  while (!work.empty()) {
    u16 pc = work.back();
    work.pop_back();
    while (in_rom(pc) && !code[pc]) {
      const DecodedInsn d = decode(family, pc, fetch_at(pc));
      for (unsigned i = 0; i < d.len; ++i) code[u16(pc + i)] = i == 0 ? 1 : 2;
      if (d.op == Op::Bcc || d.op == Op::Bsr || d.op == Op::Brset || d.op == Op::Brclr) add(d.imm, true);
      if ((d.op == Op::Jmp || d.op == Op::Jsr) && d.mode == Mode::Ext) add(d.imm, true);
      if (d.op == Op::Bcc && d.cond == 0) break;  // BRA
      if (ends_path(d.op)) break;
      pc = u16(pc + d.len);
    }
  }

  std::printf("; %s, %zu bytes at $%04X (%s)\n", path, rom.size(), base,
              hc11 ? "MC68HC11" : family == Family::HD6301 ? "HD6301" : "MC6801");
  for (unsigned v = 0; v < vectors; ++v) {
    const u16 at = u16(vector_base + 2 * v);
    std::printf("; vector %-5s $%04X\n", hc11 ? hc11_vector_names[v] : vector_names[v], (mem[at] << 8) | mem[at + 1]);
  }
  u32 a = base;
  while (a < base + rom.size()) {
    const u16 pc = u16(a);
    if (code[pc] == 1) {
      const DecodedInsn d = decode(family, pc, fetch_at(pc));
      if (labels.count(pc)) std::printf("\nL%04X:\n", pc);
      std::string text = disassemble(d);
      // Name the register field.
      const char* reg = hc11 ? hc11_register_name(d.imm) : register_name(d.imm);
      if ((d.mode == Mode::Dir || d.mode == Mode::Ext) && reg && !(hc11 && d.mode == Mode::Dir)) text += std::string("  ; ") + reg;
      u8 bytes[5];
      for (unsigned i = 0; i < d.len; ++i) bytes[i] = mem[u16(pc + i)];
      std::printf("%04X: %-10s %s\n", pc, format_bytes(bytes, d.len).c_str(), text.c_str());
      a += d.len;
      continue;
    }
    // Data: up to 16 bytes per line, stopping at code.
    std::printf("%04X: ", pc);
    std::string ascii;
    unsigned n = 0;
    while (n < 16 && a < base + rom.size() && code[u16(a)] == 0) {
      const u8 b = mem[u16(a)];
      std::printf("%02X ", b);
      ascii += (b >= 0x20 && b < 0x7F) ? char(b) : '.';
      ++a;
      ++n;
    }
    std::printf("%*s; %s\n", int(3 * (16 - n)), "", ascii.c_str());
  }
  return 0;
}

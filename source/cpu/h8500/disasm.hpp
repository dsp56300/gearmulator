// Textual disassembly of decoded instructions (Hitachi assembler syntax).
#pragma once
#include <string>

#include "cpu/h8500/decode.hpp"

namespace h8500 {

// `pc_next` is the address of the following instruction; used to print
// absolute targets for PC-relative branches.
std::string disassemble(const DecodedInsn& d, u32 pc_next = 0);

// Hex dump of the instruction bytes, e.g. "D0 21".
std::string format_bytes(const u8* bytes, unsigned length);

}  // namespace h8500

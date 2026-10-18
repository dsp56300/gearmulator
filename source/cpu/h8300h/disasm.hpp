// Textual disassembly of decoded instructions (Hitachi assembler syntax).
#pragma once
#include <string>

#include "cpu/h8300h/decode.hpp"

namespace h8300h {

// `pc_next` is the address of the following instruction; PC-relative
// branches print their absolute target from it.
std::string disassemble(const DecodedInsn& d, u32 pc_next = 0);

// Hex dump of the instruction bytes, e.g. "0D 21".
std::string format_bytes(const u8* bytes, unsigned length);

}  // namespace h8300h

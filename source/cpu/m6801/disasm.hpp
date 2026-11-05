// Textual disassembly of decoded instructions (Motorola assembler syntax).
#pragma once
#include <string>

#include "cpu/m6801/decode.hpp"

namespace m6801 {

// Branch targets are absolute in the decoded instruction already.
std::string disassemble(const DecodedInsn& d);

// Hex dump of the instruction bytes, e.g. "CE 12 34".
std::string format_bytes(const u8* bytes, unsigned length);

}  // namespace m6801

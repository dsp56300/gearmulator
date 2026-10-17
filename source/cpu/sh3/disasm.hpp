// SH-3 disassembler.
#pragma once
#include <string>

#include "cpu/sh3/decode.hpp"

namespace sh3 {

// `pc` is the instruction's address (PC-relative operands are resolved).
std::string disassemble(const DecodedInsn& d, u32 pc);
std::string disassemble(u16 code, u32 pc);

}  // namespace sh3

// SH-2 disassembler.
#pragma once
#include <string>

#include "cpu/sh2/decode.hpp"

namespace sh2 {

// `pc` is the instruction's address (PC-relative operands are resolved).
std::string disassemble(const DecodedInsn& d, u32 pc);
std::string disassemble(u16 code, u32 pc);

}  // namespace sh2

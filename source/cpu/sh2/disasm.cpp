#include "cpu/sh2/disasm.hpp"

#include "cpu/sh/disasm.hpp"

namespace sh2 {

std::string disassemble(u16 code, u32 pc) { return sh2::disassemble(decode(code), pc); }

std::string disassemble(const DecodedInsn& d, u32 pc) { return sh::disassemble(d, pc); }

}  // namespace sh2

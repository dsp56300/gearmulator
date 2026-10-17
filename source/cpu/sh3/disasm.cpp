#include "cpu/sh3/disasm.hpp"

#include "cpu/sh/disasm.hpp"

namespace sh3 {

std::string disassemble(u16 code, u32 pc) { return sh3::disassemble(decode(code), pc); }

std::string disassemble(const DecodedInsn& d, u32 pc) { return sh::disassemble(d, pc); }

}  // namespace sh3

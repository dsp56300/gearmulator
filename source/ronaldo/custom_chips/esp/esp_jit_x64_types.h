#pragma once

#include <asmjit/x86/x86operand.h>

namespace esp
{
#ifdef _MSC_VER
	static constexpr asmjit::x86::Gpq g_funcArgGPs[]     = {asmjit::x86::rcx, asmjit::x86::rdx, asmjit::x86::r8, asmjit::x86::r9};

	static constexpr asmjit::x86::Gpq g_nonVolatileGPs[] = { asmjit::x86::rbx, asmjit::x86::rbp, asmjit::x86::rdi, asmjit::x86::rsi, asmjit::x86::rsp
	                                                       , asmjit::x86::r12, asmjit::x86::r13, asmjit::x86::r14, asmjit::x86::r15};

#else
	static constexpr asmjit::x86::Gpq g_funcArgGPs[]     = { asmjit::x86::rdi, asmjit::x86::rsi, asmjit::x86::rdx, asmjit::x86::rcx, asmjit::x86::r8, asmjit::x86::r9 };

	static constexpr asmjit::x86::Gpq g_nonVolatileGPs[] = { asmjit::x86::rbx, asmjit::x86::rbp, asmjit::x86::rsp
	                                                       , asmjit::x86::r12, asmjit::x86::r13, asmjit::x86::r14, asmjit::x86::r15};
#endif

	static constexpr asmjit::x86::Gpq g_regBasePtr = asmjit::x86::r8;

	static constexpr asmjit::x86::Gpq g_poolRegs[] = { asmjit::x86::rdx, asmjit::x86::r9
		                                             , asmjit::x86::rbx, asmjit::x86::rbp, asmjit::x86::rdi, asmjit::x86::rsi
	                                                 , asmjit::x86::r12, asmjit::x86::r13, asmjit::x86::r14, asmjit::x86::r15
	                                                 , asmjit::x86::rax, asmjit::x86::r10, asmjit::x86::r11};

}

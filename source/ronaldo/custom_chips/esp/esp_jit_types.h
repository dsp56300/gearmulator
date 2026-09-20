#pragma once

#include "../jitHost.h"

#if CHIPS_JIT_X86_64
#	include "asmjit/x86/x86builder.h"
#	include "asmjit/x86/x86operand.h"
#endif

#if CHIPS_JIT_ARM64
#	include "asmjit/arm/a64builder.h"
#	include "asmjit/arm/a64operand.h"
#endif

namespace esp
{

#if CHIPS_JIT_X86_64
	using Builder = asmjit::x86::Builder;
	using RegGP = asmjit::x86::Gpq;

	class EspJitX64;
	using EspJit = EspJitX64;
#elif CHIPS_JIT_ARM64
	using Builder = asmjit::a64::Builder;
	using RegGP = asmjit::a64::GpX;

	class EspJitArm64;
	using EspJit = EspJitArm64;
#endif

}

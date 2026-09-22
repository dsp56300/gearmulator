#pragma once

// The XP DSP back end of a host without a code generator (jitHost.h): it has the interface of
// the others and never produces a frame function, so the frame driver stays on the interpreter.

#include "../jitHost.h"
#include "xp_dsp_program.h"

#include <cstddef>

namespace xpLib
{
	class DspJitBackend
	{
	public:
		static constexpr chips::JitCompile Compile = chips::JitCompile::None;
		static constexpr bool Available = Compile != chips::JitCompile::None;

		DspJitRun run() const { return nullptr; }
		size_t codeSize() const { return 0; }
		const char* lastError() const { return "no JIT back end for this host"; }
		bool compile(const FlatProgram&, const FlatProgram*) { return false; }
	};
} // namespace xpLib

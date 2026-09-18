#pragma once

// Picks the JIT backend of the host. Unlike the LSP and XP JITs there is no runtime compile:
// the programs are ROM, so they are all compiled when the ROM is loaded and a parameter change
// is a table lookup. A host without a backend runs the interpreter, which stays bit-identical.

#if defined(_M_X64) || defined(__x86_64__) || defined(__x86_64) || defined(__amd64__)
#	include "mt32reverb_jit_x86.h"
#	define MT32REVERB_JIT 1
#elif defined(__aarch64__) || defined(__ARM_ARCH_8) || defined(_M_ARM64)
#	include "mt32reverb_jit_arm64.h"
#	define MT32REVERB_JIT 1
#else
#	define MT32REVERB_JIT 0
#endif

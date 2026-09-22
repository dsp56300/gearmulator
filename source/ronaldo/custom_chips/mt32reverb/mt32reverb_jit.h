#pragma once

// Picks the JIT backend of the host. Unlike the LSP and XP JITs there is no runtime compile:
// the programs are ROM, so they are all compiled when the ROM is loaded and a parameter change
// is a table lookup. A host without a backend runs the interpreter, which stays bit-identical.

#include "../jitHost.h"
#if CHIPS_JIT_X86_64
#	include "mt32reverb_jit_x86.h"
#elif CHIPS_JIT_ARM64
#	include "mt32reverb_jit_arm64.h"
#else
#	include "mt32reverb_jit_none.h"
#endif

// For the preprocessor; code asks Jit::Available.
#define MT32REVERB_JIT (!CHIPS_JIT_NONE)

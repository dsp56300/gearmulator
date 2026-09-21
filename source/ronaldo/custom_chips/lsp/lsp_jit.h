#pragma once

// The back end of this host (../jitHost.h). Each defines LSPJIT with the same interface;
// LSPJIT::Compile tells the dispatcher how it gets its code compiled, if at all.
#include "../jitHost.h"
#if CHIPS_JIT_X86_64
#	include "lsp_jit_x86.h"
#elif CHIPS_JIT_ARM64
#	include "lsp_jit_arm64.h"
#elif CHIPS_JIT_WASM
#	include "lsp_jit_wasm.h"
#else
#	include "lsp_jit_none.h"
#endif

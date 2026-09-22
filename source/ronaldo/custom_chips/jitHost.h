#pragma once

// Which JIT back end the custom-chip emulations get on the host they are compiled for.
//
// The XP DSP, the LSP and the MT-32 reverb each pair an interpreter - the bit-exact reference -
// with code generators for the hosts we ship on. Every other host (a 32-bit x86 or ARM, RISC-V,
// WebAssembly, ...) still has to build and run, on the interpreter alone. The decision is made
// here, once, so that the chips agree and a new back end is added in one place:
//
//   1. give it a CHIPS_JIT_<host> macro below,
//   2. add <chip>_jit_<host>.h next to the existing ones and select it in <chip>_jit.h.
//
// The ESP takes its back end from here too but has no interpreter, so it builds on the x86-64
// and arm64 hosts only.
//
// Exactly one CHIPS_JIT_<host> is 1. CHIPS_JIT_WASM is the Emscripten build: the back ends emit
// WebAssembly with framework/wasmJit and the JavaScript host compiles it, possibly some time
// after it was asked to (chips::JitCompile::Host). CHIPS_JIT_NONE selects <chip>_jit_none.h, a back end with
// the same interface that never produces code: the dispatchers then stay on their interpreters
// and start no compile thread. CHIPS_FORCE_NO_JIT picks it on any host, which is how the
// interpreter-only configuration is tested without leaving a JIT host.
//
// custom_chips/CMakeLists.txt compiles this header to learn whether asmjit is needed at all.

#define CHIPS_JIT_X86_64 0
#define CHIPS_JIT_ARM64  0
#define CHIPS_JIT_WASM   0
#define CHIPS_JIT_NONE   0

#if defined(CHIPS_FORCE_NO_JIT)
#	undef  CHIPS_JIT_NONE
#	define CHIPS_JIT_NONE 1
#elif defined(_M_X64) || defined(__x86_64__) || defined(__x86_64) || defined(__amd64__)
#	undef  CHIPS_JIT_X86_64
#	define CHIPS_JIT_X86_64 1
#elif defined(__aarch64__) || defined(__ARM_ARCH_8) || defined(_M_ARM64)
#	undef  CHIPS_JIT_ARM64
#	define CHIPS_JIT_ARM64 1
#elif defined(__EMSCRIPTEN__)
#	undef  CHIPS_JIT_WASM
#	define CHIPS_JIT_WASM 1
#else
#	undef  CHIPS_JIT_NONE
#	define CHIPS_JIT_NONE 1
#endif

// The native back ends emit machine code with asmjit.
#define CHIPS_JIT_ASMJIT (CHIPS_JIT_X86_64 || CHIPS_JIT_ARM64)

namespace chips
{
	// How a back end gets its code compiled, which is what a dispatcher has to know about it.
	// Every back end says so in a constant named Compile.
	enum class JitCompile
	{
		None,	// produces no code: interpreter only, nothing to wait for
		Worker,	// compile() blocks, so it runs on a worker thread of the dispatcher
		Host,	// submit() returns at once and poll() reports when the host has compiled
	};
}

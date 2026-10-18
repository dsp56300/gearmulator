#pragma once

// The native side of Dispatcher: the host's code generator and the kernels' lifetime.
// Included by esp.cpp for the board sizes; a test that instantiates its own ERAM size
// includes it too.

#include "esp.h"
#include "esp_jit_arm64.h"
#include "esp_jit_x86.h"

namespace esp
{
#if CHIPS_JIT_ARM64
	template <int N> using JitBackend = JitArm64<N>;
#elif CHIPS_JIT_X86_64
	template <int N> using JitBackend = JitX86<N>;
#endif

#if CHIPS_JIT_ASMJIT
	// One code allocator for every ESP; asmjit's JitAllocator is thread safe.
	inline asmjit::JitRuntime& jitRuntime()
	{
		static asmjit::JitRuntime runtime;
		return runtime;
	}
#endif

	template <int lg2eramSize>
	Dispatcher<lg2eramSize>::Compiled::~Compiled()
	{
#if CHIPS_JIT_ASMJIT
		if (kernel)
			jitRuntime().release(kernel);
#endif
	}

	template <int lg2eramSize>
	typename Dispatcher<lg2eramSize>::CompiledPtr Dispatcher<lg2eramSize>::compile(const CompileRequest& request)
	{
		auto compiled = std::make_unique<Compiled>();
		compiled->key = request.key;
#if CHIPS_JIT_ASMJIT
		try
		{
			compiled->kernel = JitBackend<lg2eramSize>::compile(jitRuntime(), request.program, request.key.clocks);
		}
		catch (...)
		{
			compiled->kernel = nullptr;
		}
#endif
		return compiled;
	}
}

#include "esp_jit.h"

#include "esp_jit_types.h"

#if CHIPS_JIT_X86_64
#include "esp_jit_x64.h"
#endif

#if CHIPS_JIT_ARM64
#include "esp_jit_arm64.h"
#endif

namespace esp
{
	EspJitBase::EspJitBase()
	{
#if !CHIPS_JIT_X86_64 && !CHIPS_JIT_ARM64
		static_assert(false, "No JIT backend available for this architecture");
#endif
	}
}

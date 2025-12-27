#include "esp_jit.h"

#include "esp_jit_types.h"

#if JIT_X64
#include "esp_jit_x64.h"
#endif

#if JIT_ARM64
#include "esp_jit_arm64.h"
#endif

namespace esp
{
	EspJitBase::EspJitBase()
	{
#if !JIT_X64 && !JIT_ARM64
		static_assert(false, "No JIT backend available for this architecture");
#endif
	}
}

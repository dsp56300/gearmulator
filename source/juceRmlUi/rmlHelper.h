#pragma once

#include <cstdint>

namespace juceRmlUi
{
	template<typename TClass, typename THandle> TClass* fromHandle(const THandle _handle)
	{
		static_assert(sizeof(TClass*) == sizeof(THandle), "handle must have size of a pointer");
		return reinterpret_cast<TClass*>(static_cast<uintptr_t>(_handle));  // NOLINT(performance-no-int-to-ptr)
	}

	template<typename THandle, typename TClass> THandle toHandle(TClass* _class)
	{
		static_assert(sizeof(TClass*) == sizeof(THandle), "handle must have size of a pointer");
		return reinterpret_cast<THandle>(_class);
	}
}

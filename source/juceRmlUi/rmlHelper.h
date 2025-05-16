#pragma once

#include <cstdint>

#include "RmlUi/Core/Input.h"

namespace juce
{
	class KeyPress;
	class ModifierKeys;
	class MouseEvent;
}

namespace juceRmlUi
{
	namespace helper
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

		int toRmlModifiers(const juce::ModifierKeys _mods);
		int toRmlModifiers(const juce::MouseEvent& _e);
		int toRmlModifiers(const juce::KeyPress& _e);
		int toRmlMouseButton(const juce::MouseEvent& _e);
		Rml::Input::KeyIdentifier toRmlKey(const juce::KeyPress& _key);
	}
}

#pragma once

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/Input.h"
#include "RmlUi/Core/Vector2.h"

namespace Rml
{
	class ElementInstancer;
	class Property;
	enum class PropertyId : uint8_t;
	class Event;
	class Element;
}

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

		Rml::Vector2<float> getMousePos(const Rml::Event& _event);

		Rml::Input::KeyIdentifier getKeyIdentifier(const Rml::Event& _event);
		Rml::Input::KeyModifier getKeyModifiers(const Rml::Event& _event);
		bool getKeyModCtrl(const Rml::Event& _event);
		bool getKeyModShift(const Rml::Event& _event);
		bool getKeyModAlt(const Rml::Event& _event);

		bool isChildOf(const Rml::Element* _parent, const Rml::Element* _child);

		// apply property only if it is different from the current one
		bool changeProperty(Rml::Element* _element, Rml::PropertyId _propertyId, const Rml::Property& _property);

		Rml::ElementPtr clone(const Rml::Element* _element, Rml::ElementInstancer* _instancer);
	}
}

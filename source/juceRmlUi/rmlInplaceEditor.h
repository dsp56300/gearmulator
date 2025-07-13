#pragma once

#include <functional>
#include <string>

#include "RmlUi/Config/Config.h"

namespace Rml
{
	class Event;
}

namespace Rml
{
	class ElementFormControl;
	class Element;
}

namespace juceRmlUi
{
	class InplaceEditor
	{
	public:
		using ChangeCallback = std::function<void(const std::string&)>;
		InplaceEditor(Rml::Element* _parent, const std::string& _initialValue, ChangeCallback _changeCallback);
		~InplaceEditor();

	private:
		void onSubmit();
		void onBlur();
		void onChange(const Rml::Event& _event);

		void deleteInputElement();
		void close();

		Rml::ElementFormControl* m_input;
		std::string m_initialValue;
		ChangeCallback m_changeCallback;
	};
}

#pragma once

#include <functional>
#include <string>

namespace Rml
{
	class Event;
	class Element;
}

namespace juce
{
	class Component;
}

namespace jucePluginEditorLib
{
	class SettingsPlugin
	{
	protected:
		SettingsPlugin() = default;
	public:
		virtual ~SettingsPlugin() = default;

		virtual std::string getCategoryName() const = 0;
		virtual std::string getTemplateName() const = 0;

		virtual void createUi(Rml::Element* _root) = 0;

		static bool addClickHandler(Rml::Element* _root, const std::string& _child, std::function<void(Rml::Event&)> _handler);
	};
}

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
	class Processor;

	class SettingsPlugin
	{
	protected:
		SettingsPlugin(Processor& _processor) : m_processor(_processor) {}

	public:
		virtual ~SettingsPlugin() = default;

		virtual std::string getCategoryName() const = 0;
		virtual std::string getTemplateName() const = 0;

		virtual void createUi(Rml::Element* _root) = 0;

		static bool addClickHandler(Rml::Element* _root, const std::string& _child, std::function<void(Rml::Event&)> _handler);

	protected:
		Processor& m_processor;
	};
}

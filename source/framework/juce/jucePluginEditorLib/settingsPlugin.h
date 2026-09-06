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
	class PropertiesFile;
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

		static bool createToggleButton(Rml::Element* _root,
			const std::string& _buttonName,
			juce::PropertiesFile& _config,
			const std::string& _configName,
			const std::function<void(bool)>& _changeCallback);

	protected:
		bool createToggleButton(Rml::Element* _root,
			const std::string& _buttonName,
			const std::string& _configName,
			const std::function<void(bool)>& _changeCallback) const;

		Processor& m_processor;
	};
}

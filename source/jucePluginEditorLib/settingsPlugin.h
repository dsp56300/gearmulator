#pragma once

#include <string>

namespace Rml
{
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
	};
}

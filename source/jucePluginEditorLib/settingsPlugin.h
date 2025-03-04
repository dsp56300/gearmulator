#pragma once

#include <string>

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
		virtual juce::Component* getPage() = 0;
	};
}

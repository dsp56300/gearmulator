#pragma once

#include <string>

#include "juce_gui_basics/juce_gui_basics.h"

namespace juce
{
	class Component;
}

namespace jucePluginEditorLib
{
	class SettingsPlugin : public juce::Component
	{
	protected:
		SettingsPlugin() = default;
	public:
		~SettingsPlugin() override = default;
		virtual std::string getCategoryName() const = 0;
	};
}

#pragma once

#include <string>

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	class SettingsHeadline : public juce::Label
	{
	public:
		static constexpr int Height = 60;

		SettingsHeadline(Component* _parent, const std::string& _name);
	};
}

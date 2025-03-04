#pragma once

#include "settingsPlugin.h"

namespace jucePluginEditorLib
{
	class SettingsMidi : public SettingsPlugin
	{
	public:
		SettingsMidi() = default;

		std::string getCategoryName() const override { return "MIDI"; }

		juce::Component* getPage() override;
	};
}

#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	class Settings;

	class SettingsPage final : public juce::Component
	{
	public:
		explicit SettingsPage(Settings& _settings);
		~SettingsPage() override;
		void paint(juce::Graphics& g) override;
		void resized() override;

	private:
		Settings& m_settings;
	};
}

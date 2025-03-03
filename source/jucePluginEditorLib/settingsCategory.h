#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	class Settings;

	class SettingsCategory final : public juce::Component, public juce::LookAndFeel_V4
	{
	public:
		explicit SettingsCategory(Settings& _settings);
		~SettingsCategory() override;
		void paint(juce::Graphics& g) override;
		void resized() override;

		juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

	private:
		Settings& m_settings;

		juce::TextButton m_button;
	};
}

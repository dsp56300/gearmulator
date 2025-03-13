#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	class SettingsPlugin;
	class Settings;

	class SettingsCategory final : public juce::Component
	{
	public:
		explicit SettingsCategory(Settings& _settings, SettingsPlugin* _plugin);
		~SettingsCategory() override;
		void paint(juce::Graphics& g) override;
		void resized() override;

		void setSelected(bool _selected);
		SettingsPlugin* getPlugin() const { return m_plugin; }

	private:
		SettingsPlugin* const m_plugin;
		Settings& m_settings;

		juce::TextButton m_button;
		juce::Colour m_buttonColor;
	};
}

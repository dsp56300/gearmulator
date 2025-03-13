#pragma once

#include <memory>
#include <vector>

#include "settingsPlugin.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	class SettingsCategory;
}

namespace jucePluginEditorLib
{
	class Settings;

	class SettingsCategories : public juce::Component
	{
	public:
		explicit SettingsCategories(Settings& _settings);
		~SettingsCategories() override;
		void paint(juce::Graphics& g) override;
		void resized() override;
		void setSelectedCategory(const SettingsCategory* _settingsCategory);
		void selectLastCategory();

	private:
		void doLayout();

		Settings& m_settings;
		std::vector<std::unique_ptr<SettingsCategory>> m_categories;
		std::vector<std::unique_ptr<SettingsPlugin>> m_plugins;
	};
}

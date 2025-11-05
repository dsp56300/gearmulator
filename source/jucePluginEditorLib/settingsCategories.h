#pragma once

#include <memory>
#include <vector>

#include "settingsPlugin.h"

namespace jucePluginEditorLib
{
	class SettingsCategory;
}

namespace jucePluginEditorLib
{
	class Settings;

	class SettingsCategories
	{
	public:
		explicit SettingsCategories(Settings& _settings);
		~SettingsCategories();
		void setSelectedCategory(const SettingsCategory* _settingsCategory);
		void selectLastCategory();

	private:
		Settings& m_settings;
		std::vector<std::unique_ptr<SettingsCategory>> m_categories;
		std::vector<std::unique_ptr<SettingsPlugin>> m_plugins;
	};
}

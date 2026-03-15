#include "settingsCategories.h"

#include "pluginEditor.h"

#include "settings.h"
#include "settingsCategory.h"

namespace jucePluginEditorLib
{
	SettingsCategories::SettingsCategories(Settings& _settings) : m_settings(_settings)
	{
		_settings.getEditor().registerSettings(m_plugins);

		for (auto& p : m_plugins)
			m_categories.push_back(std::make_unique<SettingsCategory>(_settings, p.get()));
	}
	SettingsCategories::~SettingsCategories()
	{
		m_categories.clear();
		m_plugins.clear();
	}

	void SettingsCategories::setSelectedCategory(const SettingsCategory* _settingsCategory) const
	{
		for (auto & settingsCategory : m_categories)
			settingsCategory->setSelected(settingsCategory.get() == _settingsCategory);
	}

	void SettingsCategories::selectLastCategory() const
	{
		m_settings.setSelectedCategory(m_categories.front().get());
	}
}

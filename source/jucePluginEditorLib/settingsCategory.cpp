#include "settingsCategory.h"

#include "settings.h"
#include "settingsPlugin.h"

#include "juceRmlUi/rmlEventListener.h"

namespace jucePluginEditorLib
{
	SettingsCategory::SettingsCategory(Settings& _settings, SettingsPlugin* _plugin) : m_plugin(_plugin), m_settings(_settings)
	{
		m_button = _settings.createPageButton(_plugin->getCategoryName());

		juceRmlUi::EventListener::AddClick(m_button, [this]
		{
			m_settings.setSelectedCategory(this);
		});
	}

	SettingsCategory::~SettingsCategory() = default;

	void SettingsCategory::setSelected(const bool _selected) const
	{
		m_button->SetPseudoClass("checked", _selected);
	}
}

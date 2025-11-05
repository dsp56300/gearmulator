#include "settingsCategory.h"

#include "settings.h"
#include "settingsPlugin.h"

#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "RmlUi/Core/Element.h"

namespace jucePluginEditorLib
{
	SettingsCategory::SettingsCategory(Settings& _settings, SettingsPlugin* _plugin) : m_plugin(_plugin), m_settings(_settings)
	{
		m_button = _settings.createPageButton(_plugin->getCategoryName());

		m_page = juceRmlUi::helper::createTemplate(_plugin->getTemplateName(), _settings.getPageParent());

		juceRmlUi::EventListener::AddClick(m_button, [this]
		{
			m_settings.setSelectedCategory(this);
		});

		setSelected(false);
	}

	SettingsCategory::~SettingsCategory() = default;

	void SettingsCategory::setSelected(const bool _selected) const
	{
		m_button->SetPseudoClass("checked", _selected);

		if (_selected)
			m_page->RemoveProperty(Rml::PropertyId::Display);
		else
			m_page->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
	}

	Rml::Element* SettingsCategory::getPageRoot() const
	{
		return m_page;
	}
}

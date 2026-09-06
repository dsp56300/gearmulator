#include "settingsCategory.h"

#include "pluginEditor.h"
#include "pluginProcessor.h"
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

		if (!m_page)
			throw std::runtime_error("Failed to create settings page from template " + _plugin->getTemplateName());

		_plugin->createUi(m_page);

		// add device specific settings if available
		if (auto* containerDeviceSpecific = juceRmlUi::helper::findChild(m_page, "containerDeviceSpecific", false))
		{
			const auto templateName = _plugin->getTemplateName() + '_' + _settings.getEditor().getProcessor().getProperties().name;

			if (juceRmlUi::helper::hasTemplate(templateName, containerDeviceSpecific))
			{
				auto* instance = juceRmlUi::helper::createTemplate(templateName, containerDeviceSpecific);
				m_settingsDeviceSpecific = _settings.getEditor().createDeviceSpecificSettings(templateName, instance);
			}
		}

		juceRmlUi::EventListener::AddClick(m_button, [this]
		{
			m_settings.setSelectedCategory(this);
		});

		setSelected(false);
	}

	SettingsCategory::~SettingsCategory()
	{
		m_settingsDeviceSpecific.reset();
	}

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

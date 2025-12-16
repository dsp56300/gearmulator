#include "settings.h"

#include "juceRmlUi/rmlHelper.h"

#include "juce_events/juce_events.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ID.h"

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	Settings::Settings(Editor& _editor, Rml::Element* _root) : m_editor(_editor), m_root(_root), m_categories(*this), m_page(*this)
	{
		juce::MessageManager::callAsync([this]
		{
			m_categories.selectLastCategory();
		});
	}

	Settings::~Settings()
	{
		juceRmlUi::helper::removeFromParent(m_root);
	}

	void Settings::setSelectedCategory(const SettingsCategory* _settingsCategory) const
	{
		m_categories.setSelectedCategory(_settingsCategory);
	}

	std::unique_ptr<Settings> Settings::createFromTemplate(Editor& _editor, const std::string& _templateName, Rml::Element* _parent)
	{
		auto* attachedElem = juceRmlUi::helper::createTemplate(_templateName, _parent);

		return std::make_unique<Settings>(_editor, attachedElem);
	}

	Rml::Element* Settings::createPageButton(const std::string& _title) const
	{
		auto* pageButtonTemplate = juceRmlUi::helper::findChild(m_root, "btPage");
		pageButtonTemplate->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);

		auto* button = pageButtonTemplate->GetParentNode()->AppendChild(pageButtonTemplate->Clone());
		button->SetInnerRML(Rml::StringUtilities::EncodeRml(_title));
		button->RemoveProperty(Rml::PropertyId::Display);

		return button;
	}

	Rml::Element* Settings::getPageParent() const
	{
		return juceRmlUi::helper::findChild(m_root, "pageContainer");
	}
}

#include "settings.h"

#include "settingsCategory.h"

#include "Core/Template.h"
#include "Core/TemplateCache.h"

#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/CoreInstance.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/ID.h"
#include "RmlUi/Core/Log.h"
#include "RmlUi/Core/Unit.h"

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	Settings::Settings(Editor& _editor, Rml::Element* _root) : m_editor(_editor), m_root(_root), m_categories(*this), m_page(*this)
	{
//		addAndMakeVisible(m_categories);
//		addAndMakeVisible(m_page);

		juce::MessageManager::callAsync([this]
		{
			m_categories.selectLastCategory();
		});
	}

	Settings::~Settings()
	{
		juceRmlUi::helper::removeFromParent(m_root);
	}

	void Settings::setSelectedCategory(const SettingsCategory* _settingsCategory)
	{
		m_categories.setSelectedCategory(_settingsCategory);

		m_page.setPage(_settingsCategory->getPlugin());
	}

	std::unique_ptr<Settings> Settings::createFromTemplate(Editor& _editor, const std::string& _templateName, Rml::Element* _parent)
	{
		auto* t = _parent->GetCoreInstance().template_cache->GetTemplate(_templateName);

		if (!t)
		{
			Rml::Log::Message(_parent->GetCoreInstance(), Rml::Log::LT_ERROR, "Template '%s' not found", _templateName.c_str());
			return nullptr;
		}

		auto elem = _parent->GetOwnerDocument()->CreateElement("div");

		auto* parsedElem = t->ParseTemplate(elem.get());

		if (!parsedElem)
		{
			Rml::Log::Message(_parent->GetCoreInstance(), Rml::Log::LT_ERROR, "Template '%s' could not be parsed", _templateName.c_str());
			return {};
		}

		auto* attachedElem = _parent->AppendChild(std::move(elem));

		attachedElem->SetProperty(Rml::PropertyId::ZIndex, Rml::Property(100, Rml::Unit::NUMBER));

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
}

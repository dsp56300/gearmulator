#include "settings.h"

#include "pluginEditor.h"
#include "pluginProcessor.h"

#include "juceRmlUi/rmlHelper.h"

#include "juce_events/juce_events.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementUtilities.h"
#include "RmlUi/Core/ID.h"

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	Settings::Settings(Editor& _editor, Rml::Element* _root) : m_editor(_editor), m_root(_root), m_categories(*this)
	{
		const bool allowAdvanced = _editor.getProcessor().getConfig().getBoolValue("allow_advanced_options", false);

		enableAdvancedOptions(allowAdvanced);

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

	void Settings::enableAdvancedOptions(const bool _enable) const
	{
		Rml::ElementList elements;
		Rml::ElementUtilities::GetElementsByClassName(elements, m_root, "settings-advanced");

		for (auto* element : elements)
			element->SetPseudoClass("disabled", !_enable);
	}
}

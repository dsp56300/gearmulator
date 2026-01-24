#include "settings.h"

#include "pluginEditor.h"
#include "pluginProcessor.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlHelper.h"

#include "juceUiLib/messageBox.h"

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
	constexpr const char* const g_allowAdvancedOptions = "allow_advanced_options";

	Settings::Settings(Editor& _editor, Rml::Element* _root) : m_editor(_editor), m_root(_root), m_categories(*this)
	{
		auto& config = _editor.getProcessor().getConfig();

		const bool allowAdvanced = config.getBoolValue(g_allowAdvancedOptions, false);

		enableAdvancedOptions(allowAdvanced);

		juce::MessageManager::callAsync([this]
		{
			m_categories.selectLastCategory();
		});

		auto* btAllowAdvanced = juceRmlUi::helper::findChild(_root, "btEnableAdvancedOptions");

		auto* bt = juceRmlUi::helper::findChild(btAllowAdvanced, "button");

		juceRmlUi::ElemButton::setChecked(bt, allowAdvanced);

		juceRmlUi::EventListener::AddClick(btAllowAdvanced, [this, bt]
		{
			auto& c = m_editor.getProcessor().getConfig();
			const auto enabled = !c.getBoolValue(g_allowAdvancedOptions, false);

			if (!enabled)
			{
				enableAdvancedOptions(false);
				juceRmlUi::ElemButton::setChecked(bt, false);
				c.setValue(g_allowAdvancedOptions, juce::var(false));
				c.saveIfNeeded();
				return;
			}
			genericUI::MessageBox::showOkCancel(
				genericUI::MessageBox::Icon::Warning, 
				"Warning", 
				"Changing these settings may cause instability of the plugin.\n\nPlease confirm to continue.", 
				[this, bt, &c](const genericUI::MessageBox::Result _result)
				{
					if (_result == genericUI::MessageBox::Result::Ok)
					{
						enableAdvancedOptions(true);
						juceRmlUi::ElemButton::setChecked(bt, true);
						c.setValue(g_allowAdvancedOptions, juce::var(true));
						c.saveIfNeeded();
					}
				});
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

	Rml::Element* Settings::createPageButton(const std::string& _title)
	{
		if (m_pageButtonTemplate == nullptr)
		{
			m_pageButtonTemplate = juceRmlUi::helper::findChild(m_root, "btPage");
			m_pageButtonTemplate->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
		}

		auto button = m_pageButtonTemplate->Clone();

		button->SetInnerRML(Rml::StringUtilities::EncodeRml(_title));
		button->RemoveProperty(Rml::PropertyId::Display);

		return m_pageButtonTemplate->GetParentNode()->InsertBefore(std::move(button), m_pageButtonTemplate);
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

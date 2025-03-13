#include "settingsCategories.h"

#include "pluginEditor.h"
#include "settings.h"
#include "settingsCategory.h"
#include "settingsTypes.h"

namespace jucePluginEditorLib
{
	SettingsCategories::SettingsCategories(Settings& _settings) : m_settings(_settings)
	{
		_settings.getEditor().registerSettings(m_plugins);

		for (auto& p : m_plugins)
		{
			m_categories.push_back(std::make_unique<SettingsCategory>(_settings, p.get()));
			m_categories.push_back(std::make_unique<SettingsCategory>(_settings, p.get()));
			m_categories.push_back(std::make_unique<SettingsCategory>(_settings, p.get()));
		}

		for (auto& settingsCategory : m_categories)
			addAndMakeVisible(settingsCategory.get());

		doLayout();
	}
	SettingsCategories::~SettingsCategories()
	{
		m_categories.clear();
		m_plugins.clear();
	}
	void SettingsCategories::paint(juce::Graphics& g)
	{
		g.fillAll(juce::Colour(0x11ffffff));
	}

	void SettingsCategories::resized()
	{
		doLayout();
	}

	void SettingsCategories::setSelectedCategory(const SettingsCategory* _settingsCategory)
	{
		for (auto & settingsCategory : m_categories)
		{
			settingsCategory->setSelected(settingsCategory.get() == _settingsCategory);
		}
	}

	void SettingsCategories::selectLastCategory()
	{
		m_settings.setSelectedCategory(m_categories.front().get());
	}

	void SettingsCategories::doLayout()
	{
		juce::FlexBox b;
		b.justifyContent = juce::FlexBox::JustifyContent::flexStart;
		b.flexDirection = juce::FlexBox::Direction::column;
		b.flexWrap = juce::FlexBox::Wrap::noWrap;

		for (size_t i=0; i<m_categories.size(); ++i)
		{
			auto& c = m_categories[i];
			const auto m = juce::FlexItem::Margin(i ? static_cast<float>(settings::g_spacing) : 0, 0, 0, 0);
			b.items.add(juce::FlexItem(*c).withHeight(50.0f).withMargin(m));
		}

		b.performLayout(getLocalBounds().toFloat().reduced(settings::g_spacing));
	}
}

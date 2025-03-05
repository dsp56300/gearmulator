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

	void SettingsCategories::doLayout()
	{
		juce::FlexBox b;
		b.justifyContent = juce::FlexBox::JustifyContent::flexStart;
		b.flexDirection = juce::FlexBox::Direction::column;
		b.flexWrap = juce::FlexBox::Wrap::noWrap;

		for (auto& c : m_categories)
			b.items.add(juce::FlexItem(*c).withHeight(50.0f).withMargin(juce::FlexItem::Margin(10.0f)));

		b.performLayout(getLocalBounds().toFloat().reduced(10.0f));
	}
}

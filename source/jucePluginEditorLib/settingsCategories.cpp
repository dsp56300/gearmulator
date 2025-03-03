#include "settingsCategories.h"

#include "settingsCategory.h"

namespace jucePluginEditorLib
{
	SettingsCategories::SettingsCategories(Settings& _settings) : m_settings(_settings)
	{
		m_categories.push_back(std::make_unique<SettingsCategory>(_settings));
		m_categories.push_back(std::make_unique<SettingsCategory>(_settings));
		m_categories.push_back(std::make_unique<SettingsCategory>(_settings));
		m_categories.push_back(std::make_unique<SettingsCategory>(_settings));
		m_categories.push_back(std::make_unique<SettingsCategory>(_settings));
		m_categories.push_back(std::make_unique<SettingsCategory>(_settings));

		for (auto& settingsCategory : m_categories)
			addAndMakeVisible(settingsCategory.get());

		doLayout();
	}
	SettingsCategories::~SettingsCategories()
	{
		m_categories.clear();
	}
	void SettingsCategories::paint(juce::Graphics& g)
	{
		g.fillAll(juce::Colour(0x11ffffff));
	}

	void SettingsCategories::resized()
	{
		doLayout();
	}

	void SettingsCategories::doLayout()
	{
		juce::FlexBox b;
		b.justifyContent = juce::FlexBox::JustifyContent::flexStart;
		b.flexDirection = juce::FlexBox::Direction::column;
		b.flexWrap = juce::FlexBox::Wrap::noWrap;

		for (auto& c : m_categories)
			b.items.add(juce::FlexItem(*c).withHeight(100.f).withMargin(juce::FlexItem::Margin(10.0f)));

		b.performLayout(getLocalBounds().toFloat().reduced(10.0f));
	}
}

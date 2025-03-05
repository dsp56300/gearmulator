#include "settings.h"

#include "settingsCategory.h"

namespace jucePluginEditorLib
{
	Settings::Settings(Editor& _editor) : m_editor(_editor), m_categories(*this), m_page(*this)
	{
		addAndMakeVisible(m_categories);
		addAndMakeVisible(m_page);

		doLayout();
	}
	Settings::~Settings()
	= default;

	void Settings::paint(juce::Graphics& g)
	{
		auto& l = getLookAndFeel();
		l.drawPopupMenuBackground(g, getWidth(), getHeight());

		Component::paint(g);
	}

	void Settings::resized()
	{
		doLayout();
		Component::resized();
	}

	void Settings::setSelectedCategory(const SettingsCategory* _settingsCategory)
	{
		m_categories.setSelectedCategory(_settingsCategory);

		m_page.setPage(_settingsCategory->getPlugin());
	}

	void Settings::doLayout()
	{
		juce::FlexBox b;

		b.justifyContent = juce::FlexBox::JustifyContent::center;
		b.flexDirection = juce::FlexBox::Direction::row;
		b.flexWrap = juce::FlexBox::Wrap::noWrap;

		b.items.add(juce::FlexItem(m_categories).withFlex(0.2f));
		b.items.add(juce::FlexItem(m_page).withFlex(0.3f));

		b.performLayout(getLocalBounds().toFloat());
	}
}

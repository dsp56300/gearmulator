#include "settingsPage.h"

namespace jucePluginEditorLib
{
	SettingsPage::SettingsPage(Settings& _settings) : m_settings(_settings)
	{
	}
	SettingsPage::~SettingsPage() = default;

	void SettingsPage::paint(juce::Graphics& g)
	{
//		g.fillAll(juce::Colour(0x77ffffff));
	}
	void SettingsPage::resized()
	{
	}

	void SettingsPage::setPage(Component* _page)
	{
		if (m_page)
			removeChildComponent(m_page);
		m_page = _page;
		if (m_page)
		{
			addAndMakeVisible(m_page);
			m_page->setBounds(getLocalBounds());
		}
		resized();
	}
}

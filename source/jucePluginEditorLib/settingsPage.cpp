#include "settingsPage.h"

#include "settingsTypes.h"

namespace jucePluginEditorLib
{
	SettingsPage::SettingsPage(Settings& _settings) : m_settings(_settings)
	{
		m_viewport.reset(new juce::Viewport());
		m_viewport->setScrollBarPosition(true, false);
		addAndMakeVisible(m_viewport.get());
	}
	SettingsPage::~SettingsPage() = default;

	void SettingsPage::paint(juce::Graphics& g)
	{
//		g.fillAll(juce::Colour(0x77ffffff));
	}
	void SettingsPage::resized()
	{
		m_viewport->setBounds(getLocalBounds().reduced(settings::g_spacing, settings::g_spacing));
		if (m_page)
		{
			// we resize once with a very large height and then shrink the height to fit all page children
			auto bounds = m_viewport->getLocalBounds().withHeight(5000).withWidth(m_viewport->getWidth() - m_viewport->getScrollBarThickness() - 1);
			m_page->setBounds(bounds);

			bounds = bounds.withHeight(0);

			for(auto* child : m_page->getChildren())
			{
				auto bottom = child->getBottom();
				bounds = bounds.withBottom(std::max(bottom, bounds.getBottom()));
			}
			m_page->setBounds(bounds);
		}
	}

	void SettingsPage::setPage(Component* _page)
	{
		if (m_page)
			m_viewport->removeChildComponent(m_page);
		m_page = _page;
		if (m_page)
		{
			m_viewport->setViewedComponent(m_page, false);
			m_page->setBounds(m_viewport->getLocalBounds().withHeight(5000));
		}
		resized();
	}
}

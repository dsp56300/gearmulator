#include "settingsPage.h"

namespace jucePluginEditorLib
{
	SettingsPage::SettingsPage(Settings& _settings) : m_settings(_settings)
	{
	}
	SettingsPage::~SettingsPage()
	{
	}
	void SettingsPage::paint(juce::Graphics& g)
	{
//		g.fillAll(juce::Colour(0x77ffffff));
	}
	void SettingsPage::resized()
	{
	}
}

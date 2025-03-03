#include "settingsCategory.h"

namespace jucePluginEditorLib
{
	SettingsCategory::SettingsCategory(Settings& _settings) : m_settings(_settings)
	{
		addAndMakeVisible(m_button);

		m_button.setButtonText("Test");
		m_button.setLookAndFeel(this);

		SettingsCategory::resized();
	}
	SettingsCategory::~SettingsCategory()
	{
	}

	void SettingsCategory::paint(juce::Graphics& g)
	{
//		g.fillAll(juce::Colour(0x77ffffff));
	}

	void SettingsCategory::resized()
	{
		m_button.centreWithSize(getWidth() - 20, getHeight() - 20);
	}

	juce::Font SettingsCategory::getTextButtonFont(juce::TextButton& _textButton, int buttonHeight)
	{
	    return { static_cast<float>(buttonHeight) * 0.7f };
	}
}

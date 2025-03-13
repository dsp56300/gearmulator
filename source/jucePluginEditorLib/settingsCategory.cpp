#include "settingsCategory.h"

#include "settings.h"
#include "settingsPlugin.h"
#include "settingsStyles.h"

namespace jucePluginEditorLib
{
	SettingsCategory::SettingsCategory(Settings& _settings, SettingsPlugin* _plugin) : m_plugin(_plugin), m_settings(_settings)
	{
		addAndMakeVisible(m_button);

		m_button.setButtonText(m_plugin->getCategoryName());
		m_button.setLookAndFeel(&settings::getStyle());
		m_button.onClick = [this]
		{
			m_settings.setSelectedCategory(this);
		};

		m_buttonColor = m_button.findColour(juce::TextButton::buttonColourId);

		SettingsCategory::resized();
	}
	SettingsCategory::~SettingsCategory() = default;

	void SettingsCategory::paint(juce::Graphics& g)
	{
//		g.fillAll(juce::Colour(0x77ffffff));
	}

	void SettingsCategory::resized()
	{
		m_button.centreWithSize(getWidth(), getHeight());
	}

	void SettingsCategory::setSelected(const bool _selected)
	{
		m_button.setColour(juce::TextButton::buttonColourId, _selected ? m_buttonColor.brighter(0.2f) : m_buttonColor);
	}
}

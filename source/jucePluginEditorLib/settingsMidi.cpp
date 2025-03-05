#include "settingsMidi.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	SettingsMidi::SettingsMidi()
	{
		m_midiIn = new juce::ComboBox();
		m_midiOut = new juce::ComboBox();
		m_midiInLabel = new juce::Label();
		m_midiOutLabel = new juce::Label();

		m_midiInLabel->setText("MIDI Input", juce::dontSendNotification);
		m_midiOutLabel->setText("MIDI Output", juce::dontSendNotification);

		m_midiIn->setName("MidiIn");
		m_midiOut->setName("MidiOut");

		addAndMakeVisible(m_midiInLabel);
		addAndMakeVisible(m_midiIn);
		addAndMakeVisible(m_midiOutLabel);
		addAndMakeVisible(m_midiOut);
	}

	void SettingsMidi::resized()
	{
		SettingsPlugin::resized();

		juce::Grid g;
		g.items.add(juce::GridItem(*m_midiInLabel));
		g.items.add(juce::GridItem(*m_midiOutLabel));
		g.items.add(juce::GridItem(*m_midiIn));
		g.items.add(juce::GridItem(*m_midiOut));
		g.justifyContent = juce::Grid::JustifyContent::start;
		g.alignContent = juce::Grid::AlignContent::start;
		g.columnGap = juce::Grid::Px(5.0f);
		g.rowGap = juce::Grid::Px(5.0f);

		g.templateRows    = {juce::Grid::TrackInfo(juce::Grid::Fr(1)), juce::Grid::TrackInfo(juce::Grid::Fr(1))};
		g.templateColumns = {juce::Grid::TrackInfo(juce::Grid::Fr(1)), juce::Grid::TrackInfo(juce::Grid::Fr(1))};

		g.performLayout(getLocalBounds().reduced(10).withHeight(60));
		/*
		juce::FlexBox b;
		b.justifyContent = juce::FlexBox::JustifyContent::flexStart;
		b.flexDirection = juce::FlexBox::Direction::row;
		b.flexWrap = juce::FlexBox::Wrap::noWrap;
		b.items.add(juce::FlexItem(*m_midiIn).withWidth(200.0f).withMargin(10.0f));
		b.items.add(juce::FlexItem(*m_midiOut).withWidth(200.0f).withMargin(10.0f));
		b.performLayout(getLocalBounds().toFloat().reduced(10.0f).withHeight(50.0f));
		*/
	}
}

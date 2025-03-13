#include "settingsMidi.h"

#include "midiPorts.h"
#include "pluginProcessor.h"
#include "settingsStyles.h"
#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	namespace settings
	{
		class Style;
	}

	SettingsMidi::SettingsMidi(pluginLib::Processor& _processor) : m_processor(_processor)
	{
		m_midiIn.reset(new juce::ComboBox());
		m_midiOut.reset(new juce::ComboBox());
		m_midiInLabel.reset(new juce::Label());
		m_midiOutLabel.reset(new juce::Label());

		settings::getStyle().apply(m_midiInLabel.get());
		settings::getStyle().apply(m_midiOutLabel.get());

		m_midiInLabel->setText("MIDI Input", juce::dontSendNotification);
		m_midiOutLabel->setText("MIDI Output", juce::dontSendNotification);

		m_midiIn->setName("MidiIn");
		m_midiOut->setName("MidiOut");

		jucePluginEditorLib::MidiPorts::initInputComboBox(m_processor.getMidiPorts(), m_midiIn.get());
		jucePluginEditorLib::MidiPorts::initOutputComboBox(m_processor.getMidiPorts(), m_midiOut.get());

		addAndMakeVisible(m_midiInLabel.get());
		addAndMakeVisible(m_midiIn.get());
		addAndMakeVisible(m_midiOutLabel.get());
		addAndMakeVisible(m_midiOut.get());
	}

	SettingsMidi::~SettingsMidi() = default;

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

		g.performLayout(getLocalBounds().withHeight(60));
	}
}

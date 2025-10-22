#include "settingsMidi.h"

#include "midiPorts.h"
#include "pluginProcessor.h"
#include "settingsMidiMatrix.h"
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
		m_headerPorts.reset(new SettingsHeadline(this, "MIDI Ports"));

		m_midiIn.reset(new juce::ComboBox());
		m_midiOut.reset(new juce::ComboBox());
		m_midiInLabel.reset(new juce::Label());
		m_midiOutLabel.reset(new juce::Label());

		settings::getStyle().apply(m_midiInLabel.get());
		settings::getStyle().apply(m_midiOutLabel.get());

		m_midiInLabel->setText("Input", juce::dontSendNotification);
		m_midiOutLabel->setText("Output", juce::dontSendNotification);

		m_midiIn->setName("MidiIn");
		m_midiOut->setName("MidiOut");

//		MidiPorts::initInputComboBox(m_processor.getMidiPorts(), m_midiIn.get());
//		MidiPorts::initOutputComboBox(m_processor.getMidiPorts(), m_midiOut.get());

		addAndMakeVisible(m_midiInLabel.get());
		addAndMakeVisible(m_midiIn.get());
		addAndMakeVisible(m_midiOutLabel.get());
		addAndMakeVisible(m_midiOut.get());

		m_headerRouting.reset(new SettingsHeadline(this, "MIDI Routing"));

		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::Note, "Note"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::SysEx, "SysEx"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::Controller, "Controller"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::PolyPressure, "PolyPressure"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::Aftertouch, "Aftertouch"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::PitchBend, "PitchBend"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::ProgramChange, "ProgramChange"));
		m_matrices.push_back(std::make_unique<SettingsMidiMatrix>(*this, synthLib::MidiRoutingMatrix::EventType::Other, "Other"));

		for (const auto& matrix : m_matrices)
			addAndMakeVisible(matrix.get());
	}

	SettingsMidi::~SettingsMidi() = default;

	void SettingsMidi::resized()
	{
		SettingsPlugin::resized();

		m_headerPorts->setBounds(0, 0, getWidth(), SettingsHeadline::Height);

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

		int y = 60;
		g.performLayout(getLocalBounds().withHeight(y).withY(80));
		y += 80;

		y += 20;
		m_headerRouting->setBounds(0, y, getWidth(), SettingsHeadline::Height);
		y += SettingsHeadline::Height;
		y += 20;

		for (auto& m : m_matrices)
		{
			m->setBounds(0, y, getWidth(), 180);
			y += 200;
		}
	}
}

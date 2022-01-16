#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VirusController.h"
#include "version.h"

#include "ui/VirusEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p) :
	AudioProcessorEditor(&p), processorRef(p), m_parameterBinding(p), m_virusEditor(new VirusEditor(m_parameterBinding, processorRef)), m_scale("Scale")
{
    ignoreUnused (processorRef);

	setSize(1377, 800);
	const auto config = processorRef.getController().getConfig();
	auto scale = config->getIntValue("scale", 100);
	m_virusEditor->setTopLeftPosition(0, 0);
	m_scale.setBounds(8,8,64,24);
	m_scale.addItem("50%", 50);
	m_scale.addItem("75%", 75);
	m_scale.addItem("100%", 100);
	m_scale.addItem("125%", 125);
	m_scale.addItem("150%", 150);
	m_scale.addItem("200%", 200);

	m_scale.setSelectedId(scale, juce::dontSendNotification);
	m_scale.setColour(juce::ComboBox::textColourId, juce::Colours::white);
	m_scale.onChange = [this, config]() {
		float value = m_scale.getSelectedIdAsValue().getValue();
		setScaleFactor(value/100.0f);
		config->setValue("scale", (int)value);
		config->saveIfNeeded();
	};
	setScaleFactor(scale/100.0f);
	addAndMakeVisible(m_virusEditor);
	addAndMakeVisible(m_scale);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
	// ugly (polling!) way for refreshing presets names as this is temporary ui
}

void AudioPluginAudioProcessorEditor::resized()
{
	// This is generally where you'll want to lay out the positions of any
	// subcomponents in your editor..
	auto area = getLocalBounds();
	area.removeFromTop(35);
}

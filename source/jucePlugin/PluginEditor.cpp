#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VirusController.h"
#include "version.h"

#include "ui/VirusEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p) :
	AudioProcessorEditor(&p), processorRef(p), m_parameterBinding(p), m_virusEditor(new VirusEditor(m_parameterBinding, processorRef))
{
    ignoreUnused (processorRef);

	setSize(1377, 800);
	m_virusEditor->setTopLeftPosition(0, 0);
	addAndMakeVisible(m_virusEditor);
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

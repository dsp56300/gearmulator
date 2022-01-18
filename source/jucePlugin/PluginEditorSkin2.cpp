#include "PluginProcessor.h"
#include "PluginEditorSkin2.h"
#include "VirusController.h"
#include "version.h"
// AH TODO: need compiler switch
#include "ui2/VirusEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p) :
	AudioProcessorEditor(&p), processorRef(p), m_parameterBinding(p), m_virusEditor(new VirusEditor(m_parameterBinding, processorRef))
{
	double dScaleFactor = 0.75;
	const auto config = processorRef.getController().getConfig();
	
	setResizable(true,false);
	dScaleFactor = config->getDoubleValue("skin_scale_factor", 0.75f);
	setScaleFactor(dScaleFactor);
	setSize(m_virusEditor->iSkinSizeWidth, m_virusEditor->iSkinSizeHeight);
	m_virusEditor->m_AudioPlugInEditor = (AudioPluginAudioProcessorEditor*)this;

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

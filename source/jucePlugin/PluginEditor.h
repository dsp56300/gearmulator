#pragma once

#include "VirusParameterBinding.h"
#include "ui/VirusEditor.h"
#include "PluginProcessor.h"
#include <juce_audio_devices/juce_audio_devices.h>
//==============================================================================
class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
	//void handleIncomingMidiMessage(juce::MidiInput *source, const juce::MidiMessage &message) override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
	void timerCallback() override;

	// This reference is provided as a quick way for your editor to
	// access the processor object that created it.
	AudioPluginAudioProcessor& processorRef;
	VirusParameterBinding m_parameterBinding;

	// New "real" editor
	VirusEditor *m_virusEditor;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)

};

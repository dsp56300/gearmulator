#pragma once

#include "VirusParameterBinding.h"

#include "PluginProcessor.h"

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
	void LoadSkin(int index);
	// This reference is provided as a quick way for your editor to
	// access the processor object that created it.
	AudioPluginAudioProcessor& processorRef;
	VirusParameterBinding m_parameterBinding;

	std::unique_ptr<juce::Component> m_virusEditor;
	juce::ComboBox m_scale;
	juce::ComboBox m_skin;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)

};

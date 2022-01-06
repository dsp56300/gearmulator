#pragma once

#include "VirusParameterBinding.h"
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
	void updateMidiInput(int index);
	void updateMidiOutput(int index);
	void timerCallback() override;
	void loadFile();

	// This reference is provided as a quick way for your editor to
	// access the processor object that created it.
	AudioPluginAudioProcessor& processorRef;
	VirusParameterBinding m_parameterBinding;
	juce::GenericAudioProcessorEditor m_tempEditor;
	juce::TextButton m_partSelectors[16];
	juce::TextButton m_prevPatch[16];
	juce::TextButton m_nextPatch[16];
	juce::TextButton m_btSingleMode;
	juce::TextButton m_btMultiMode;
	juce::TextButton m_btLoadFile;
	juce::String m_previousPath;
	juce::ComboBox m_cmbMidiInput;
	juce::ComboBox m_cmbMidiOutput;

	juce::AudioDeviceManager deviceManager;
	juce::PropertiesFile *m_properties;
	int m_lastInputIndex = 0;
	int m_lastOutputIndex = 0;

	// New "real" editor
	juce::TextButton m_openEditor; // temporary until integrated - will be rebased!
	std::unique_ptr<juce::ResizableWindow> m_virusEditor;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)

};

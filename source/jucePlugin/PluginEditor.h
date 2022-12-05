#pragma once

#include "VirusParameterBinding.h"

class PluginEditorState;

//==============================================================================
class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&, PluginEditorState&);
    ~AudioPluginAudioProcessorEditor() override;

	void mouseDown(const juce::MouseEvent& event) override;

	void paint(juce::Graphics& g) override {}

private:
	void setGuiScale(int percent);
	void openMenu();
	void setUiRoot(juce::Component* _component);

	// This reference is provided as a quick way for your editor to
	// access the processor object that created it.
	AudioPluginAudioProcessor& processorRef;

	PluginEditorState& m_state;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};

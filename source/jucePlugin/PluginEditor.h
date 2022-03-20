#pragma once

#include "VirusParameterBinding.h"

#include "PluginProcessor.h"

//==============================================================================
class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
	void mouseDown(const juce::MouseEvent& event) override;

private:
	void timerCallback() override;
	void loadSkin(int index);
	void setGuiScale(int percent);
	void openMenu();
	void exportCurrentSkin() const;

	// This reference is provided as a quick way for your editor to
	// access the processor object that created it.
	AudioPluginAudioProcessor& processorRef;

	VirusParameterBinding m_parameterBinding;

	std::unique_ptr<juce::Component> m_virusEditor;
	int m_currentSkinId = -1;
	float m_rootScale = 1.0f;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};

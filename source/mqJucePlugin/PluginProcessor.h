#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "../synthLib/plugin.h"
#include "../jucePluginEditorLib/pluginProcessor.h"

#include "../mqLib/device.h"

class PluginEditorState;

//==============================================================================
class AudioPluginAudioProcessor  : public jucePluginEditorLib::Processor
{
public:
    //==============================================================================
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

	// _____________
	//
	bool isPluginValid() const { return m_plugin.isValid(); }

    synthLib::Plugin& getPlugin() override
    {
	    return m_plugin;
    }
    void updateLatencySamples();

	bool setLatencyBlocks(uint32_t _blocks) override;

    pluginLib::Controller* createController() override;
private:

    //==============================================================================
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)

	mqLib::Device		    			m_device;
	synthLib::Plugin					m_plugin;
    std::unique_ptr<PluginEditorState>  m_editorState;
};

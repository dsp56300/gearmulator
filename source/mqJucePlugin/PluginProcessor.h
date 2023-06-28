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
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;

	// _____________
	//
    synthLib::Device* createDevice() override;

    void updateLatencySamples() override;

    pluginLib::Controller* createController() override;

	void loadCustomData(const std::vector<uint8_t>& _sourceBuffer) override;
	void saveCustomData(std::vector<uint8_t>& _targetBuffer) override;

	float getOutputGain() const { return m_outputGain; }
	void setOutputGain(const float _gain) { m_outputGain = _gain; }

private:

    //==============================================================================
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)

    std::unique_ptr<PluginEditorState>  m_editorState;

	float m_inputGain = 1.0f;
	float m_outputGain = 1.0f;
};

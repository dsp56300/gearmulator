#pragma once

#include "../synthLib/plugin.h"
#include "../virusLib/device.h"

#include "VirusController.h"

#include "../jucePluginEditorLib/pluginProcessor.h"

class PluginEditorState;

//==============================================================================
class AudioPluginAudioProcessor : public jucePluginEditorLib::Processor
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
    std::string getRomName() const
    {
        return juce::File(juce::String(m_romName)).getFileNameWithoutExtension().toStdString();
    }
    virusLib::ROMFile::Model getModel() const
    {
		return m_rom.getModel();
    }

    bool setLatencyBlocks(uint32_t _blocks) override;
	// _____________
	//
private:

    synthLib::Plugin& getPlugin() override
    {
	    return m_plugin;
    }

    pluginLib::Controller* createController() override;

    void updateLatencySamples();

    //==============================================================================
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)

	std::string							m_romName;
	virusLib::ROMFile					m_rom;
	virusLib::Device					m_device;
	synthLib::Plugin					m_plugin;
    uint32_t							m_clockTempoParam = 0xffffffff;
    std::unique_ptr<PluginEditorState>  m_editorState;
};

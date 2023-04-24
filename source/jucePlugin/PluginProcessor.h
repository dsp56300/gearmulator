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

	std::string getRomName() const
    {
        return juce::File(juce::String(m_rom.getFilename())).getFileNameWithoutExtension().toStdString();
    }
    virusLib::ROMFile::Model getModel() const
    {
		return m_rom.getModel();
    }

	// _____________
	//
private:
	void updateLatencySamples() override;

    synthLib::Plugin& getPlugin() override
    {
	    return m_plugin;
    }

    pluginLib::Controller* createController() override;

    //==============================================================================
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)

	virusLib::ROMFile					m_rom;
	virusLib::Device					m_device;
	synthLib::Plugin					m_plugin;
    uint32_t							m_clockTempoParam = 0xffffffff;
    std::unique_ptr<PluginEditorState>  m_editorState;
};

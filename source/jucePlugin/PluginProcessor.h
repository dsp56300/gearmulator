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
    jucePluginEditorLib::PluginEditorState* createEditorState() override;
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;

	// _____________
	//

	std::string getRomName() const
    {
		if(!m_rom)
			return "<invalid>";
        return juce::File(juce::String(m_rom->getFilename())).getFileNameWithoutExtension().toStdString();
    }
    virusLib::ROMFile::Model getModel() const
    {
		return m_rom ? m_rom->getModel() : virusLib::ROMFile::Model::Invalid;
    }

	// _____________
	//
private:
	void updateLatencySamples() override;

    synthLib::Device* createDevice() override;

    pluginLib::Controller* createController() override;

    //==============================================================================
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)

	std::unique_ptr<virusLib::ROMFile>	m_rom;

	uint32_t							m_clockTempoParam = 0xffffffff;
    std::unique_ptr<PluginEditorState>  m_editorState;
};

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "../synthLib/plugin.h"
#include "../virusLib/device.h"
#include "VirusController.h"

//==============================================================================
class AudioPluginAudioProcessor  : public juce::AudioProcessor, juce::MidiInputCallback
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

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    void getCurrentProgramStateInformation (juce::MemoryBlock& destData) override;
    void setCurrentProgramStateInformation (const void* data, int sizeInBytes) override;

	// _____________
	//
    Virus::Controller &getController();
	bool isPluginValid() const { return m_plugin.isValid(); }
	void getLastMidiOut(std::vector<synthLib::SMidiEvent>& dst);
	void addMidiEvent(const synthLib::SMidiEvent& ev);
	bool setMidiOutput(const juce::String& _out);
	juce::MidiOutput* getMidiOutput() const;
	bool setMidiInput(const juce::String& _in);
	juce::MidiInput* getMidiInput() const;
	void handleIncomingMidiMessage(juce::MidiInput *source, const juce::MidiMessage &message) override;
	
    std::string getRomName() {
        return juce::File(juce::String(m_romName)).getFileNameWithoutExtension().toStdString();
    }
    virusLib::ROMFile::Model getModel() const
    {
		return m_rom.getModel();
    }
    synthLib::Plugin& getPlugin()
    {
	    return m_plugin;
    }
    void updateLatencySamples();

	void setLatencyBlocks(uint32_t _blocks);

	// _____________
	//
private:
    std::unique_ptr<Virus::Controller> m_controller;
	std::unique_ptr<juce::MidiOutput> m_midiOutput;
	std::unique_ptr<juce::MidiInput> m_midiInput;
    void setState(const void *_data, size_t _sizeInBytes);

    //==============================================================================
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)

	std::string							m_romName;
	virusLib::ROMFile					m_rom;
	virusLib::Device					m_device;
	synthLib::Plugin					m_plugin;
	std::vector<synthLib::SMidiEvent>	m_midiOut;
    uint32_t							m_clockTempoParam = 0xffffffff;
};

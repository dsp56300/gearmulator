#pragma once

#include "../synthLib/plugin.h"
#include "../virusLib/device.h"

#include "../jucePluginLib/event.h"

#include "VirusController.h"

#include "../jucePluginEditorLib/pluginProcessor.h"

//==============================================================================
class AudioPluginAudioProcessor : public jucePluginEditorLib::Processor
{
public:
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    jucePluginEditorLib::PluginEditorState* createEditorState() override;

    void processBpm(float _bpm) override;

	// _____________
	//

	std::string getRomName() const
    {
        const auto* rom = getSelectedRom();
        if(!rom)
			return "<invalid>";
        return juce::File(juce::String(rom->getFilename())).getFileNameWithoutExtension().toStdString();
    }

    const virusLib::ROMFile* getSelectedRom() const
	{
        if(m_selectedRom >= m_roms.size())
            return {};
        return &m_roms[m_selectedRom];
	}

    virusLib::DeviceModel getModel() const
    {
        auto* rom = getSelectedRom();
		return rom ? rom->getModel() : virusLib::DeviceModel::Invalid;
    }

    const auto& getRoms() const { return m_roms; }

    bool setSelectedRom(uint32_t _index);
    uint32_t getSelectedRomIndex() const { return m_selectedRom; }

    uint32_t getPartCount() const
    {
	    return getModel() == virusLib::DeviceModel::Snow ? 4 : 16;
    }

	// _____________
	//
private:
    synthLib::Device* createDevice() override;

    pluginLib::Controller* createController() override;

    void saveChunkData(synthLib::BinaryStream& s) override;
    void loadChunkData(synthLib::ChunkReader& _cr) override;

    //==============================================================================
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)

	std::vector<virusLib::ROMFile>      m_roms;
    uint32_t                            m_selectedRom = 0;

	uint32_t							m_clockTempoParam = 0xffffffff;

public:
    pluginLib::Event<const virusLib::ROMFile*> evRomChanged;
};

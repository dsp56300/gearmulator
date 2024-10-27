#pragma once

#include "synthLib/plugin.h"
#include "virusLib/device.h"

#include "jucePluginLib/event.h"

#include "VirusController.h"

#include "jucePluginEditorLib/pluginProcessor.h"

namespace virus
{
	class VirusProcessor : public jucePluginEditorLib::Processor
	{
	public:
	    VirusProcessor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions, const pluginLib::Processor::Properties& _properties, virusLib::DeviceModel _defaultModel);
	    ~VirusProcessor() override;

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

	protected:
	    void postConstruct(std::vector<virusLib::ROMFile>&& _roms);

	    // _____________
		//
	private:
	    synthLib::Device* createDevice() override;

	    pluginLib::Controller* createController() override;

	    void saveChunkData(baseLib::BinaryStream& s) override;
	    void loadChunkData(baseLib::ChunkReader& _cr) override;

		void zynthianExportLv2Presets() const;
		void exportLv2Presets(const virusLib::ROMFile& _rom, const std::string& _rootPath) const;

	    //==============================================================================
		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VirusProcessor)

		std::vector<virusLib::ROMFile>      m_roms;
	    const virusLib::DeviceModel			m_defaultModel;
	    uint32_t                            m_selectedRom = 0;

		uint32_t							m_clockTempoParam = 0xffffffff;

	public:
	    pluginLib::Event<const virusLib::ROMFile*> evRomChanged;
	};
}

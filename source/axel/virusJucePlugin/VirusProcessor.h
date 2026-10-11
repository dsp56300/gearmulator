#pragma once

#include "synthLib/plugin.h"
#include "virusLib/device.h"

#include "baseLib/event.h"

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
	        if(getFirmwareIndex() >= m_roms.size())
	            return {};
	        return &m_roms[getFirmwareIndex()];
		}

	    virusLib::DeviceModel getModel() const
	    {
	        auto* rom = getSelectedRom();
			return rom ? rom->getModel() : virusLib::DeviceModel::Invalid;
	    }

	    const auto& getRoms() const { return m_roms; }

	    uint32_t getPartCount() const
	    {
		    return getModel() == virusLib::DeviceModel::Snow ? 4 : 16;
	    }

	protected:
	    void postConstruct(std::vector<virusLib::ROMFile>&& _roms);

	    // _____________
		//
	private:
	    synthLib::Device* newDevice(const synthLib::DeviceCreateParams& _params) override;
		void getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const override;

	    pluginLib::Controller* createController() override;

		void zynthianExportLv2Presets() const;
		void exportLv2Presets(const virusLib::ROMFile& _rom, const std::string& _rootPath) const;

	    //==============================================================================
		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VirusProcessor)

		std::vector<virusLib::ROMFile>      m_roms;
	    const virusLib::DeviceModel			m_defaultModel;

		uint32_t							m_clockTempoParam = 0xffffffff;

		baseLib::EventListener<uint32_t>	m_onFirmwareChanged;
	};
}

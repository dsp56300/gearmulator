#pragma once

#include <functional>
#include <memory>
#include <mutex>

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
	        return getRom(getFirmwareIndex());
		}

	    virusLib::DeviceModel getModel() const
	    {
	        auto* rom = getSelectedRom();
			return rom ? rom->getModel() : virusLib::DeviceModel::Invalid;
	    }

	    uint32_t getPartCount() const
	    {
		    return getModel() == virusLib::DeviceModel::Snow ? 4 : 16;
	    }

	protected:
	    // _findRoms is called again by a reboot that the user asks for
	    void postConstruct(std::function<std::vector<virusLib::ROMFile>()> _findRoms);

	    // _____________
		//
	private:
		const virusLib::ROMFile* getRom(uint32_t _index) const;
		std::vector<std::string> findNewFirmwareImages() override;

	    synthLib::Device* newDevice(const synthLib::DeviceCreateParams& _params) override;
		void getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const override;

	    pluginLib::Controller* createController() override;

		void zynthianExportLv2Presets() const;
		void exportLv2Presets(const virusLib::ROMFile& _rom, const std::string& _rootPath) const;

	    //==============================================================================
		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VirusProcessor)

		std::function<std::vector<virusLib::ROMFile>()> m_findRoms;
		// Append only, each ROM in an allocation of its own: the loader thread of the patch manager and the audio thread
		// use the selected one while a reboot appends the ROMs it found
		std::vector<std::unique_ptr<const virusLib::ROMFile>> m_roms;
		mutable std::mutex					m_romsMutex;
	    const virusLib::DeviceModel			m_defaultModel;

		baseLib::EventListener<uint32_t>	m_onFirmwareChanged;
	};
}

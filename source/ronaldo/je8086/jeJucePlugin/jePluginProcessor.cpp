#include "jePluginProcessor.h"

#include "jeController.h"
#include "jePluginEditorState.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "jeLib/device.h"
#include "jeLib/romloader.h"
#include "jucePluginLib/processorPropertiesInit.h"

#include "synthLib/deviceException.h"

namespace jeJucePlugin
{
	class Controller;

	AudioPluginAudioProcessor::AudioPluginAudioProcessor() :
	    Processor(BusesProperties()
	                   .withOutput("Out AB", juce::AudioChannelSet::stereo(), true)
	                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
		, {}, pluginLib::initProcessorProperties())
	{
		m_roms = jeLib::RomLoader::findROMs();

		// default to keyboard for now
		m_selectedRom = std::numeric_limits<size_t>::max();
		for (size_t i=0; i<m_roms.size(); ++i)
		{
			if (m_roms[i].getDeviceType() != jeLib::DeviceType::Keyboard)
				continue;
			m_selectedRom = i;
			break;
		}

		// FIXME clear rom list if there is no keyboard rom to prevent that the rack rom is used
		if (m_selectedRom == std::numeric_limits<size_t>::max())
		{
			m_roms.clear();
			m_selectedRom = 0;
		}

		getController();
		const auto latencyBlocks = getConfig().getIntValue("latencyBlocks", static_cast<int>(getPlugin().getLatencyBlocks()));
		Processor::setLatencyBlocks(latencyBlocks);
	}

	AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
	{
		destroyEditorState();
	}

	jucePluginEditorLib::PluginEditorState* AudioPluginAudioProcessor::createEditorState()
	{
		return new PluginEditorState(*this);
	}

	synthLib::Device* AudioPluginAudioProcessor::createDevice()
	{
		const auto& rom = getSelectedRom();

		if (!rom.isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "A firmware rom (512k .bin) is required, but was not found.");

		synthLib::DeviceCreateParams params;

		params.romData = rom.getData();
		params.romName = rom.getName();
		params.homePath = getDataFolder();

		auto* d = new jeLib::Device(params);
		if(!d->isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "A firmware rom (512k .bin) is required, but was not found.");
		return d;
	}

	void AudioPluginAudioProcessor::getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const
	{
		Processor::getRemoteDeviceParams(_params);
/*
		auto rom = je::RomLoader::findROM();

		if(rom.isValid())
		{
			_params.romData.assign(rom.data().begin(), rom.data().end());
			_params.romName = rom.getFilename();
		}
*/	}

	pluginLib::Controller* AudioPluginAudioProcessor::createController()
	{
		return new jeJucePlugin::Controller(*this);
	}

	const jeLib::Rom& AudioPluginAudioProcessor::getSelectedRom() const
	{
		if (m_roms.empty())
		{
			static jeLib::Rom emptyRom;
			return emptyRom;
		}
		if (getSelectedRomIndex() >= m_roms.size())
			return m_roms.back();
		return m_roms[m_selectedRom];
	}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new jeJucePlugin::AudioPluginAudioProcessor();
}

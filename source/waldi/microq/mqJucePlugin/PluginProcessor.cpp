#include "PluginProcessor.h"
#include "PluginEditorState.h"

#include "mqController.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "jucePluginLib/processorPropertiesInit.h"

#include "baseLib/binarystream.h"

#include "mqLib/device.h"
#include "mqLib/romloader.h"

namespace
{
	juce::PropertiesFile::Options getOptions()
	{
		juce::PropertiesFile::Options opts;
		opts.applicationName = "DSP56300EmulatorVavra";
		opts.filenameSuffix = ".settings";
		opts.folderName = "DSP56300EmulatorVavra";
		opts.osxLibrarySubFolder = "Application Support/DSP56300EmulatorVavra";
		return opts;
	}
}

namespace mqJucePlugin
{
	class Controller;

	AudioPluginAudioProcessor::AudioPluginAudioProcessor() :
	    Processor(BusesProperties()
	                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
	                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#if JucePlugin_IsSynth
	                   .withOutput("Out 2", juce::AudioChannelSet::stereo(), true)
	                   .withOutput("Out 3", juce::AudioChannelSet::stereo(), true)
#endif
		, getOptions(), pluginLib::initProcessorProperties())
	{
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
		synthLib::DeviceCreateParams p;
		getRemoteDeviceParams(p);
		return new mqLib::Device(p);
	}

	void AudioPluginAudioProcessor::getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const
	{
		Processor::getRemoteDeviceParams(_params);

		if (m_voiceExpansion)
			_params.customData |= 1;

		const auto rom = mqLib::RomLoader::findROM();

		if(rom.isValid())
		{
			_params.romData = rom.getData();
			_params.romName = rom.getFilename();
		}
	}

	pluginLib::Controller* AudioPluginAudioProcessor::createController()
	{
		return new Controller(*this);
	}

	void AudioPluginAudioProcessor::setVoiceExpansion(const bool _enabled)
	{
		if (m_voiceExpansion == _enabled)
			return;
		m_voiceExpansion = _enabled;
		rebootDevice();
	}

	void AudioPluginAudioProcessor::saveChunkData(baseLib::BinaryStream& s)
	{
		if (m_voiceExpansion)
		{
			baseLib::ChunkWriter cw(s, "VEXP", 1);
			s.write<uint8_t>(m_voiceExpansion ? 1 : 0);
		}
		Processor::saveChunkData(s);
	}

	void AudioPluginAudioProcessor::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("VEXP", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t)
		{
			m_voiceExpansion = _binaryStream.read<uint8_t>() != 0;
		});

		Processor::loadChunkData(_cr);
	}

	bool AudioPluginAudioProcessor::loadCustomData(const std::vector<uint8_t>& _sourceBuffer)
	{
		const auto prevVE = m_voiceExpansion;
		const auto result = Processor::loadCustomData(_sourceBuffer);
		if (m_voiceExpansion != prevVE)
			rebootDevice();
		return result;
	}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mqJucePlugin::AudioPluginAudioProcessor();
}

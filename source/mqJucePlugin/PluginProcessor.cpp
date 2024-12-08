#include "PluginProcessor.h"
#include "PluginEditorState.h"

#include "mqController.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "jucePluginLib/processorPropertiesInit.h"

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
		return new mqLib::Device({});
	}

	void AudioPluginAudioProcessor::getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const
	{
		Processor::getRemoteDeviceParams(_params);

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
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mqJucePlugin::AudioPluginAudioProcessor();
}

#include "PluginProcessor.h"
#include "PluginEditorState.h"
#include "BinaryData.h"

#include "mqController.h"

#include "jucePluginLib/processor.h"

#include "mqLib/device.h"

namespace
{
	juce::PropertiesFile::Options getOptions()
	{
		juce::PropertiesFile::Options opts;
		opts.applicationName = "vavra";
		opts.filenameSuffix = "settings";
#ifdef JUCE_LINUX 
  	opts.folderName = ".config/The Usual Suspects/Vavra";
#else
  	opts.folderName = "The Usual Suspects/Vavra";
#endif
		opts.osxLibrarySubFolder = "Application Support";
		return opts;
	}

	pluginLib::Processor::BinaryDataRef getBinaryData()
	{
		return
		{
			BinaryData::namedResourceListSize,
			BinaryData::originalFilenames,
			BinaryData::namedResourceList,
			BinaryData::getNamedResource
		};
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
		, getOptions(), pluginLib::Processor::Properties{JucePlugin_Name, JucePlugin_Manufacturer, JucePlugin_IsSynth, JucePlugin_WantsMidiInput, JucePlugin_ProducesMidiOutput, JucePlugin_IsMidiEffect, JucePlugin_Lv2Uri, getBinaryData()})
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
		return new mqLib::Device();
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

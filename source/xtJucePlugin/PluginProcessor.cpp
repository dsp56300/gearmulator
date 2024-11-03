#include "PluginProcessor.h"

#include "PluginEditorState.h"
#include "xtController.h"

#include "BinaryData.h"

#include "jucePluginLib/processor.h"
#include "xtLib/xtDevice.h"

class Controller;

namespace
{
	juce::PropertiesFile::Options getOptions()
	{
		juce::PropertiesFile::Options opts;
		opts.applicationName = "xenia";
		opts.filenameSuffix = "settings";
#ifdef JUCE_LINUX 
  	opts.folderName = ".config/The Usual Suspects/Xenia";
#else
  	opts.folderName = "The Usual Suspects/Xenia";
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

namespace xtJucePlugin
{
	AudioPluginAudioProcessor::AudioPluginAudioProcessor() :
	    Processor(BusesProperties()
	                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
	                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#if JucePlugin_IsSynth
	                   .withOutput("Out 2", juce::AudioChannelSet::stereo(), true)
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
		return new xt::Device();
	}

	pluginLib::Controller* AudioPluginAudioProcessor::createController()
	{
		return new Controller(*this);
	}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new xtJucePlugin::AudioPluginAudioProcessor();
}

#include "OsirusProcessor.h"
#include "OsirusEditorState.h"
#include "BinaryData.h"

#include "virusLib/romloader.h"

namespace
{
	juce::PropertiesFile::Options getConfigOptions()
	{
		juce::PropertiesFile::Options opts;
		opts.applicationName = "osirus";
		opts.filenameSuffix = "settings";
#ifdef JUCE_LINUX 
  	opts.folderName = ".config/The Usual Suspects/Osirus";
#else
  	opts.folderName = "The Usual Suspects/Osirus";
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

//==============================================================================
OsirusProcessor::OsirusProcessor() :
    VirusProcessor(BusesProperties()
                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#if JucePlugin_IsSynth
                   .withOutput("Out 2", juce::AudioChannelSet::stereo(), true)
                   .withOutput("Out 3", juce::AudioChannelSet::stereo(), true)
#endif
	, ::getConfigOptions(), pluginLib::Processor::Properties{JucePlugin_Name, JucePlugin_Manufacturer, JucePlugin_IsSynth, JucePlugin_WantsMidiInput, JucePlugin_ProducesMidiOutput, JucePlugin_IsMidiEffect, JucePlugin_Lv2Uri, getBinaryData()}
	, virusLib::DeviceModel::ABC)
{
	postConstruct(virusLib::ROMLoader::findROMs(virusLib::DeviceModel::ABC));
}

OsirusProcessor::~OsirusProcessor()
{
	destroyEditorState();
}

jucePluginEditorLib::PluginEditorState* OsirusProcessor::createEditorState()
{
	return new OsirusEditorState(*this, getController());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OsirusProcessor();
}

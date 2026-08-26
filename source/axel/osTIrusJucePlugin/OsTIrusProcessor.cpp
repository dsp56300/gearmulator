#include "OsTIrusProcessor.h"
#include "OsTIrusEditorState.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "jucePluginLib/processorPropertiesInit.h"

#include "virusLib/romloader.h"

namespace
{
	juce::PropertiesFile::Options getConfigOptions()
	{
		juce::PropertiesFile::Options opts;
		opts.applicationName = "DSP56300Emulator_OsTIrus";
		opts.filenameSuffix = ".settings";
		opts.folderName = "DSP56300Emulator_OsTIrus";
		opts.osxLibrarySubFolder = "Application Support/DSP56300Emulator_OsTIrus";
		return opts;
	}
}

//==============================================================================
OsTIrusProcessor::OsTIrusProcessor() :
    VirusProcessor(BusesProperties()
                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#if JucePlugin_IsSynth
                   .withOutput("Out 2", juce::AudioChannelSet::stereo(), true)
                   .withOutput("Out 3", juce::AudioChannelSet::stereo(), true)
                   .withOutput("USB 1", juce::AudioChannelSet::stereo(), true)
                   .withOutput("USB 2", juce::AudioChannelSet::stereo(), true)
                   .withOutput("USB 3", juce::AudioChannelSet::stereo(), true)
#endif
	, ::getConfigOptions(), pluginLib::initProcessorProperties()
	, virusLib::DeviceModel::TI2)
{
	postConstruct(virusLib::ROMLoader::findROMs(virusLib::DeviceModel::TI2, virusLib::DeviceModel::Snow));
}

OsTIrusProcessor::~OsTIrusProcessor()
{
	destroyEditorState();
}

jucePluginEditorLib::PluginEditorState* OsTIrusProcessor::createEditorState()
{
	return new OsTIrusEditorState(*this, getController());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OsTIrusProcessor();
}

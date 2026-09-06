#include "OsirusProcessor.h"
#include "OsirusEditorState.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "jucePluginLib/processorPropertiesInit.h"

#include "virusLib/romloader.h"

namespace
{
	juce::PropertiesFile::Options getConfigOptions()
	{
		juce::PropertiesFile::Options opts;
		opts.applicationName = "DSP56300 Emulator";
		opts.filenameSuffix = ".settings";
		opts.folderName = "DSP56300 Emulator";
		opts.osxLibrarySubFolder = "Application Support/DSP56300 Emulator";
		return opts;
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
	, ::getConfigOptions(), pluginLib::initProcessorProperties()
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

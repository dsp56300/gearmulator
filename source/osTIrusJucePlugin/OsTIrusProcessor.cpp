#include "OsTIrusProcessor.h"
#include "OsTIrusEditorState.h"
#include "BinaryData.h"

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
	, ::getConfigOptions(), pluginLib::Processor::Properties{JucePlugin_Name, JucePlugin_IsSynth, JucePlugin_WantsMidiInput, JucePlugin_ProducesMidiOutput, JucePlugin_IsMidiEffect}
	, virusLib::ROMLoader::findROMs(virusLib::DeviceModel::TI2, virusLib::DeviceModel::Snow), virusLib::DeviceModel::TI2)
{
	postConstruct();
}

OsTIrusProcessor::~OsTIrusProcessor()
{
	destroyEditorState();
}

const char* OsTIrusProcessor::findEmbeddedResource(const char* _name, uint32_t& _size) const
{
	for(size_t i=0; i<BinaryData::namedResourceListSize; ++i)
	{
		if (std::string(BinaryData::originalFilenames[i]) != std::string(_name))
			continue;

		int size = 0;
		const auto res = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
		_size = static_cast<uint32_t>(size);
		return res;
	}
	return nullptr;
}

jucePluginEditorLib::PluginEditorState* OsTIrusProcessor::createEditorState()
{
	return new OsTIrusEditorState(*this, getController());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OsTIrusProcessor();
}

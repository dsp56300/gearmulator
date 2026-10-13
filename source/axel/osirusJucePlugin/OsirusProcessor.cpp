#include "OsirusProcessor.h"

#include "virusJucePlugin/VirusEditor.h"
#include "jucePluginEditorLib/pluginEditorState.h"
#include "jucePluginEditorLib/pluginEntry.h"
#include "skins.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "jucePluginLib/processorPropertiesInit.h"

#include "virusLib/romloader.h"

//==============================================================================
OsirusProcessor::OsirusProcessor() :
    VirusProcessor(BusesProperties()
                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#if JucePlugin_IsSynth
                   .withOutput("Out 2", juce::AudioChannelSet::stereo(), true)
                   .withOutput("Out 3", juce::AudioChannelSet::stereo(), true)
#endif
	, legacyConfigOptions("DSP56300 Emulator"), pluginLib::initProcessorProperties()
	, virusLib::DeviceModel::ABC)
{
	postConstruct(virusLib::ROMLoader::findROMs(virusLib::DeviceModel::ABC));
}

jucePluginEditorLib::PluginEditorState* OsirusProcessor::createEditorState()
{
	return new jucePluginEditorLib::PluginEditorStateT<genericVirusUI::VirusEditor, virus::VirusProcessor>(*this, g_includedSkins);
}

TUS_PLUGIN_ENTRY(OsirusProcessor)

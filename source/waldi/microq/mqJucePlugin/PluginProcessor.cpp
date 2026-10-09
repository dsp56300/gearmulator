#include "PluginProcessor.h"
#include "mqEditor.h"
#include "jucePluginEditorLib/pluginEditorState.h"
#include "jucePluginEditorLib/pluginEntry.h"
#include "skins.h"

#include "mqController.h"
#include "mqPatchManager.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "jucePluginLib/processorPropertiesInit.h"

#include "mqLib/device.h"
#include "mqLib/romloader.h"

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
		, legacyConfigOptions("DSP56300EmulatorVavra"), pluginLib::initProcessorProperties())
	{
		setPatchManager(new PatchManager(*this));
	}

	jucePluginEditorLib::PluginEditorState* AudioPluginAudioProcessor::createEditorState()
	{
		return new jucePluginEditorLib::PluginEditorStateT<mqJucePlugin::Editor>(*this, g_includedSkins);
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

TUS_PLUGIN_ENTRY(mqJucePlugin::AudioPluginAudioProcessor)

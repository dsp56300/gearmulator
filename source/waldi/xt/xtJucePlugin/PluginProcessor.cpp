#include "PluginProcessor.h"

#include "xtEditor.h"
#include "jucePluginEditorLib/pluginEditorState.h"
#include "jucePluginEditorLib/pluginEntry.h"
#include "skins.h"
#include "xtController.h"
#include "xtPatchManager.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "jucePluginLib/processorPropertiesInit.h"

#include "xtLib/xtDevice.h"

#include "xtLib/xtRomLoader.h"

class Controller;

namespace xtJucePlugin
{
	AudioPluginAudioProcessor::AudioPluginAudioProcessor() :
	    Processor(BusesProperties()
	                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
	                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#if JucePlugin_IsSynth
	                   .withOutput("Out 2", juce::AudioChannelSet::stereo(), true)
#endif
		, legacyConfigOptions("DSP56300EmulatorXenia"), pluginLib::initProcessorProperties())
	{
		setPatchManager(new PatchManager(*this));
	}

	jucePluginEditorLib::PluginEditorState* AudioPluginAudioProcessor::createEditorState()
	{
		return new jucePluginEditorLib::PluginEditorStateT<xtJucePlugin::Editor>(*this, g_includedSkins);
	}

	synthLib::Device* AudioPluginAudioProcessor::createDevice()
	{
		synthLib::DeviceCreateParams p;
		getRemoteDeviceParams(p);
		return new xt::Device(p);
	}

	void AudioPluginAudioProcessor::getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const
	{
		Processor::getRemoteDeviceParams(_params);

		const auto rom = xt::RomLoader::findROM();

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

TUS_PLUGIN_ENTRY(xtJucePlugin::AudioPluginAudioProcessor)

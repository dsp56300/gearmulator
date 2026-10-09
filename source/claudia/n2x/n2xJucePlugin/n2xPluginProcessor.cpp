#include "n2xPluginProcessor.h"

#include "n2xController.h"
#include "n2xPatchManager.h"
#include "n2xEditor.h"
#include "jucePluginEditorLib/pluginEditorState.h"
#include "jucePluginEditorLib/pluginEntry.h"
#include "skins.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "jucePluginLib/processorPropertiesInit.h"

#include "n2xLib/n2xdevice.h"
#include "n2xLib/n2xromloader.h"

#include "synthLib/deviceException.h"

namespace n2xJucePlugin
{
	class Controller;

	AudioPluginAudioProcessor::AudioPluginAudioProcessor() :
	    Processor(BusesProperties()
	                   .withOutput("Out AB", juce::AudioChannelSet::stereo(), true)
	                   .withOutput("Out CD", juce::AudioChannelSet::stereo(), true)
		, legacyConfigOptions("DSP56300EmulatorNodalRed"), pluginLib::initProcessorProperties())
	{
		setPatchManager(new PatchManager(*this));
	}

	jucePluginEditorLib::PluginEditorState* AudioPluginAudioProcessor::createEditorState()
	{
		return new jucePluginEditorLib::PluginEditorStateT<n2xJucePlugin::Editor>(*this, g_includedSkins);
	}

	synthLib::Device* AudioPluginAudioProcessor::createDevice()
	{
		auto* d = new n2x::Device({});
		if(!d->isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "A firmware rom (512k .bin) is required, but was not found.");
		return d;
	}

	void AudioPluginAudioProcessor::getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const
	{
		Processor::getRemoteDeviceParams(_params);

		auto rom = n2x::RomLoader::findROM();

		if(rom.isValid())
		{
			_params.romData.assign(rom.data().begin(), rom.data().end());
			_params.romName = rom.getFilename();
		}
	}

	pluginLib::Controller* AudioPluginAudioProcessor::createController()
	{
		return new n2xJucePlugin::Controller(*this);
	}
}

TUS_PLUGIN_ENTRY(n2xJucePlugin::AudioPluginAudioProcessor)

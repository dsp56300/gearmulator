#pragma once

#include "jucePluginEditorLib/pluginProcessor.h"

namespace xtJucePlugin
{
	class AudioPluginAudioProcessor  : public jucePluginEditorLib::Processor
	{
	public:
	    AudioPluginAudioProcessor();
	    ~AudioPluginAudioProcessor() override;

	    jucePluginEditorLib::PluginEditorState* createEditorState() override;

		synthLib::Device* createDevice() override;

	    pluginLib::Controller* createController() override;

	private:
		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
	};
}

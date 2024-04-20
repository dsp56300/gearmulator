#pragma once

#include "../virusJucePlugin/PluginEditorState.h"

class AudioPluginAudioProcessor;

class OsTIrusEditorState : public PluginEditorState
{
public:
	explicit OsTIrusEditorState(AudioPluginAudioProcessor& _processor, pluginLib::Controller& _controller);
};

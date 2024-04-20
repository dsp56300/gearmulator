#pragma once

#include "../virusJucePlugin/pluginEditorState.h"

class AudioPluginAudioProcessor;

class OsirusEditorState : public PluginEditorState
{
public:
	explicit OsirusEditorState(AudioPluginAudioProcessor& _processor, pluginLib::Controller& _controller);
};

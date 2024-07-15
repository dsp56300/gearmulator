#pragma once

#include "virusJucePlugin/VirusEditorState.h"

class VirusProcessor;

class OsirusEditorState : public VirusEditorState
{
public:
	explicit OsirusEditorState(VirusProcessor& _processor, pluginLib::Controller& _controller);
};

#pragma once

#include "virusJucePlugin/VirusEditorState.h"

class VirusProcessor;

class OsTIrusEditorState : public VirusEditorState
{
public:
	explicit OsTIrusEditorState(VirusProcessor& _processor, pluginLib::Controller& _controller);
};

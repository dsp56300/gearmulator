#pragma once

#include "virusJucePlugin/VirusEditorState.h"

class OsirusEditorState : public virus::VirusEditorState
{
public:
	explicit OsirusEditorState(virus::VirusProcessor& _processor, pluginLib::Controller& _controller);
};

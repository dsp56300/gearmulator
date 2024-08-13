#pragma once

#include "virusJucePlugin/VirusEditorState.h"

class OsTIrusEditorState : public virus::VirusEditorState
{
public:
	explicit OsTIrusEditorState(virus::VirusProcessor& _processor, pluginLib::Controller& _controller);
};

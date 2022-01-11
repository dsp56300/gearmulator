#pragma once

#include "../PluginProcessor.h"
#include "Virus_Buttons.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "../VirusController.h"
class VirusParameterBinding;

class Parts : public juce::Component
{
	public:
		Parts(VirusParameterBinding& _parameterBinding, Virus::Controller& _controller);
	private:
		Virus::Controller &m_controller;
		VirusParameterBinding &m_parameterBinding;
};
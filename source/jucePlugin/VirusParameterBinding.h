#pragma once
#include "VirusController.h"

namespace juce {
	class Value;
}

class AudioPluginAudioProcessor;

class VirusParameterBinding
{
public:
	VirusParameterBinding(AudioPluginAudioProcessor& _processor) : m_processor(_processor)
	{
	}

	void bind(juce::Slider& _control, Virus::ParameterType _param) const;

	AudioPluginAudioProcessor& m_processor;
};

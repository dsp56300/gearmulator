#pragma once
#include "VirusController.h"

namespace juce {
	class Value;
}

class AudioPluginAudioProcessor;

class VirusParameterBinding
{
public:
	VirusParameterBinding(AudioPluginAudioProcessor& _processor, uint8_t _part = 0) : m_processor(_processor)
	{
		m_part = _part;
	}

	void bind(juce::Slider& _control, Virus::ParameterType _param) const;
	void bind(juce::ComboBox &_control, Virus::ParameterType _param) const;
	void bind(juce::DrawableButton &_control, Virus::ParameterType _param) const;
	void bind(juce::Component &_control, Virus::ParameterType _param) const;
	AudioPluginAudioProcessor& m_processor;
	uint8_t m_part;
};

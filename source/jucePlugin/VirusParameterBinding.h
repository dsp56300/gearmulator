#pragma once
#include "VirusController.h"
#include "VirusParameter.h"
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
	void setPart(uint8_t _part);
	void bind(juce::Slider& _control, Virus::ParameterType _param);
	void bind(juce::ComboBox &_control, Virus::ParameterType _param);
	void bind(juce::DrawableButton &_control, Virus::ParameterType _param);
	void bind(juce::Component &_control, Virus::ParameterType _param);
	AudioPluginAudioProcessor& m_processor;
	uint8_t m_part;
	juce::Array<Virus::Parameter*> m_bindings;
};

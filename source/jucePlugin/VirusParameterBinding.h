#pragma once
#include "VirusController.h"
#include "VirusParameter.h"
#include "ui/Virus_Buttons.h"
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
	~VirusParameterBinding();
	void clearBindings();
	void setPart(uint8_t _part);
	void bind(juce::Slider& _control, Virus::ParameterType _param);
	void bind(juce::Slider& _control, Virus::ParameterType _param, uint8_t _part);
	void bind(juce::ComboBox &_control, Virus::ParameterType _param);
	void bind(juce::ComboBox &_control, Virus::ParameterType _param, uint8_t _part);
	void bind(juce::DrawableButton &_control, Virus::ParameterType _param);
	void bind(juce::Component &_control, Virus::ParameterType _param);
	AudioPluginAudioProcessor& m_processor;
	juce::Array<Virus::Parameter*> m_bindings;
};

#pragma once
#include "VirusController.h"
#include "VirusParameter.h"
#include "ui/Virus_Buttons.h"
namespace juce {
	class Value;
}

class AudioPluginAudioProcessor;
class Parameter;
class VirusParameterBindingMouseListener : public juce::MouseListener
{
public:
	VirusParameterBindingMouseListener(Virus::Parameter* _param, juce::Slider &_slider) : m_param(_param), m_slider(&_slider) {
	}
	Virus::Parameter *m_param;
	juce::Slider* m_slider;
	void VirusParameterBindingMouseListener::mouseDown(const juce::MouseEvent &event) { m_param->beginChangeGesture(); };
	void VirusParameterBindingMouseListener::mouseUp(const juce::MouseEvent &event) { m_param->endChangeGesture(); };
	void VirusParameterBindingMouseListener::mouseDrag(const juce::MouseEvent &event) { m_param->setValueNotifyingHost(m_param->convertTo0to1((float)m_slider->getValue())); };
};

class VirusParameterBinding : juce::MouseListener
{
public:
	VirusParameterBinding(AudioPluginAudioProcessor &_processor) : m_processor(_processor)
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

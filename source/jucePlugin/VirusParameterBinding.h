#pragma once

#include "VirusParameter.h"

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
	void mouseDown(const juce::MouseEvent &event) override { m_param->beginChangeGesture(); }
	void mouseUp(const juce::MouseEvent &event) override { m_param->endChangeGesture(); }
	void mouseDrag(const juce::MouseEvent &event) override { m_param->setValueNotifyingHost(m_param->convertTo0to1(static_cast<float>(m_slider->getValue()))); }
};

class VirusParameterBinding final : juce::MouseListener
{
public:
	VirusParameterBinding(AudioPluginAudioProcessor &_processor) : m_processor(_processor)
	{

	}
	~VirusParameterBinding() override;
	void clearBindings();
	void setPart(uint8_t _part);
	void bind(juce::Slider& _control, Virus::ParameterType _param);
	void bind(juce::Slider& _control, Virus::ParameterType _param, uint8_t _part);
	void bind(juce::ComboBox &_control, Virus::ParameterType _param);
	void bind(juce::ComboBox &_control, Virus::ParameterType _param, uint8_t _part);
	void bind(juce::Button &_control, Virus::ParameterType _param);

private:
	void removeMouseListener(juce::Slider& _slider);

	AudioPluginAudioProcessor& m_processor;

	static constexpr uint8_t CurrentPart = 0xff;

	struct BoundParameter
	{
		Virus::Parameter* parameter = nullptr;
		juce::Component* component = nullptr;
		Virus::ParameterType type = Virus::Param_Invalid;
		uint8_t part = CurrentPart;
	};

	std::vector<BoundParameter> m_bindings;
	std::map<juce::Slider*, MouseListener*> m_sliderMouseListeners;
};

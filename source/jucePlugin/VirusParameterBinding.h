#pragma once

#include "../jucePluginLib/parameter.h"

namespace juce {
	class Value;
}

class AudioPluginAudioProcessor;
class Parameter;
class VirusParameterBindingMouseListener : public juce::MouseListener
{
public:
	VirusParameterBindingMouseListener(pluginLib::Parameter* _param, juce::Slider &_slider) : m_param(_param), m_slider(&_slider) {
	}
	pluginLib::Parameter *m_param;
	juce::Slider* m_slider;
	void mouseDown(const juce::MouseEvent &event) override { m_param->beginChangeGesture(); }
	void mouseUp(const juce::MouseEvent &event) override { m_param->endChangeGesture(); }
	void mouseDrag(const juce::MouseEvent &event) override { m_param->setValueNotifyingHost(m_param->convertTo0to1(static_cast<float>(m_slider->getValue()))); }
};

class VirusParameterBinding final : juce::MouseListener
{
public:
	static constexpr uint8_t CurrentPart = 0xff;

	struct BoundParameter
	{
		pluginLib::Parameter* parameter = nullptr;
		juce::Component* component = nullptr;
		uint32_t type = 0xffffffff;
		uint8_t part = CurrentPart;
		uint32_t onChangeListenerId = 0;
	};

	VirusParameterBinding(AudioPluginAudioProcessor &_processor) : m_processor(_processor)
	{
	}
	~VirusParameterBinding() override;

	void bind(juce::Slider& _control, uint32_t _param);
	void bind(juce::Slider& _control, uint32_t _param, uint8_t _part);
	void bind(juce::ComboBox &_control, uint32_t _param);
	void bind(juce::ComboBox &_control, uint32_t _param, uint8_t _part);
	void bind(juce::Button &_control, uint32_t _param);

	void clearBindings();
	void setPart(uint8_t _part);

	void disableBindings();
	void enableBindings();

	const auto& getBindings() const { return m_bindings; }

private:
	void removeMouseListener(juce::Slider& _slider);

	void disableBinding(const BoundParameter& _value);

	AudioPluginAudioProcessor& m_processor;

	void bind(const std::vector<BoundParameter>& _bindings, bool _currentPartOnly);

	std::vector<BoundParameter> m_bindings;
	std::vector<BoundParameter> m_disabledBindings;
	std::map<juce::Slider*, MouseListener*> m_sliderMouseListeners;
	uint32_t m_nextListenerId = 100000;
};

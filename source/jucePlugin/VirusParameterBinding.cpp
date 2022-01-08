#include "VirusParameterBinding.h"
#include "VirusParameter.h"
#include "PluginProcessor.h"

void VirusParameterBinding::setPart(uint8_t _part) {
	m_part = _part;

	for (const auto b : m_bindings)
	{
		b->onValueChanged = nullptr;
	}
	m_bindings.clear();
	
}
void VirusParameterBinding::bind(juce::Slider &_slider, Virus::ParameterType _param)
{
	const auto v = m_processor.getController().getParameter(_param, m_part);
	if (!v)
	{
		assert(false && "Failed to find parameter");
		return;
	}
	const auto range = v->getNormalisableRange();
	_slider.setRange(range.start, range.end, range.interval);
	_slider.setDoubleClickReturnValue(true, v->getDefaultValue());
	_slider.getValueObject().referTo(v->getValueObject());
}

void VirusParameterBinding::bind(juce::ComboBox& _combo, Virus::ParameterType _param)
{
	const auto v = m_processor.getController().getParameter(_param, m_part);
	if (!v)
	{
		assert(false && "Failed to find parameter");
		return;
	}
	_combo.setTextWhenNothingSelected("--");
	_combo.addItemList(v->getAllValueStrings(), 1);
	_combo.setSelectedItemIndex(v->getValueObject().getValueSource().getValue());
	_combo.onChange = [this, &_combo, v]() {
		//v->setValue(_combo.getSelectedId() - 1);
		v->getValueObject().getValueSource().setValue(_combo.getSelectedItemIndex());
	};

	v->onValueChanged = [this, &_combo, v]() { _combo.setSelectedItemIndex(v->getValueObject().getValueSource().getValue(), juce::NotificationType::dontSendNotification); };
	m_bindings.add(v);
}

void VirusParameterBinding::bind(juce::DrawableButton &_btn, Virus::ParameterType _param)
{
	const auto v = m_processor.getController().getParameter(_param, m_part);
	if (!v)
	{
		assert(false && "Failed to find parameter");
		return;
	}
	_btn.getToggleStateValue().referTo(v->getValueObject());
}

void VirusParameterBinding::bind(juce::Component &_btn, Virus::ParameterType _param)
{
	const auto v = m_processor.getController().getParameter(_param, m_part);
	if (!v)
	{
		assert(false && "Failed to find parameter");
		return;
	}

}

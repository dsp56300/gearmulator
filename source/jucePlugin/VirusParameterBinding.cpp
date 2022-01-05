#include "VirusParameterBinding.h"

#include "PluginProcessor.h"

void VirusParameterBinding::bind(juce::Slider& _slider, Virus::ParameterType _param) const
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

void VirusParameterBinding::bind(juce::ComboBox& _combo, Virus::ParameterType _param) const
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
		v->setValue(_combo.getSelectedItemIndex());
	};
}

void VirusParameterBinding::bind(juce::DrawableButton &_btn, Virus::ParameterType _param) const
{
	const auto v = m_processor.getController().getParameter(_param, m_part);
	if (!v)
	{
		assert(false && "Failed to find parameter");
		return;
	}
	_btn.getToggleStateValue().referTo(v->getValueObject());
}

void VirusParameterBinding::bind(juce::Component &_btn, Virus::ParameterType _param) const
{
	const auto v = m_processor.getController().getParameter(_param, m_part);
	if (!v)
	{
		assert(false && "Failed to find parameter");
		return;
	}

}

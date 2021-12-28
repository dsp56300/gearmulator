#include "VirusParameterBinding.h"

#include "PluginProcessor.h"

void VirusParameterBinding::bind(juce::Slider& _slider, Virus::ParameterType _param) const
{
	const auto v = m_processor.getController().getParameter(_param);
	if (!v)
	{
		assert(false && "Failed to find parameter");
		return;
	}
	const auto range = v->getNormalisableRange();
	_slider.setRange(range.start, range.end, range.interval);
	_slider.getValueObject().referTo(v->getValueObject());
}

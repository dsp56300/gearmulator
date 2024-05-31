#include "slider.h"

namespace genericUI
{
	void Slider::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
	{
		// we use the default behaviour if ctrl/cmd is not pressed. If it is, we want to inc/dec single steps
		if(!event.mods.isCommandDown() || !isEnabled())
		{
			juce::Slider::mouseWheelMove(event, wheel);
			return;
		}

		const auto range = getNormalisableRange();

		if(range.end <= range.start)
			return;

		const auto mouseDelta = (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY) * (wheel.isReversed ? -1.0f : 1.0f);

		const auto diff = range.interval > 1.0 ? range.interval : 1.0;

		if(mouseDelta > 0)
			setValue(getValue() + diff);
		else
			setValue(getValue() - diff);
	}
}

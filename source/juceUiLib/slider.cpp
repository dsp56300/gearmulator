#include "slider.h"

namespace genericUI
{
	// Juce default is about 32 steps for the mouse wheel to go from min to max
	constexpr int g_minStepsForDefaultWheel = 32;

	void Slider::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
	{
		const auto range = getNormalisableRange();

		// we use the default behaviour if ctrl/cmd is not pressed and the range is large enough
		if((range.end - range.start) > g_minStepsForDefaultWheel && (!event.mods.isCommandDown() || !isEnabled()))
		{
			juce::Slider::mouseWheelMove(event, wheel);
			return;
		}

		// Otherwise inc/dec single steps

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

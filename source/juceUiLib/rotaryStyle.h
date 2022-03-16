#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "uiObjectStyle.h"

namespace genericUI
{
	class RotaryStyle : public UiObjectStyle
	{
		void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
	};
}

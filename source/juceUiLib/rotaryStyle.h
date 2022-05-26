#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "uiObjectStyle.h"

namespace genericUI
{
	class RotaryStyle : public UiObjectStyle
	{
	public:
		enum class Style
		{
			Rotary,
			LinearVertical,
			LinearHorizontal
		};

		explicit RotaryStyle(Editor& _editor) : UiObjectStyle(_editor) {}

		void apply(Editor& _editor, const UiObject& _object) override;

		Style getStyle() const { return m_style; }

	private:
		void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
		void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, const juce::Slider::SliderStyle, juce::Slider&) override;
		int getSliderThumbRadius(juce::Slider&) override;

		Style m_style = Style::Rotary;
	};
}

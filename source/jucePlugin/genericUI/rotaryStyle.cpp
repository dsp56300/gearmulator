#include "rotarystyle.h"

namespace genericUI
{
	void RotaryStyle::drawRotarySlider(juce::Graphics& _graphics, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& _slider)
	{
        if(!m_drawable || !m_tileSizeX || !m_tileSizeY)
            return;

//		const auto w = m_drawable->getWidth();
		const auto h = m_drawable->getHeight();

//		const auto stepsX = w / m_tileSizeX;
		const auto stepsY = h / m_tileSizeY;

//      const auto stepX = juce::roundToInt(static_cast<float>(stepsX - 1) * sliderPosProportional);
		const auto stepY = juce::roundToInt(static_cast<float>(stepsY - 1) * sliderPosProportional);

		m_drawable->setOriginWithOriginalSize({0.0f, static_cast<float>(-m_tileSizeY * stepY)});

		m_drawable->drawAt(_graphics, static_cast<float>(x), static_cast<float>(y), 1.0f);
	}
}

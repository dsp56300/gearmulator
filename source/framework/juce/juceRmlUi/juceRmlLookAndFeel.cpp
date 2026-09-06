#include "juceRmlLookAndFeel.h"

namespace juceRmlUi
{
	std::unique_ptr<juce::LowLevelGraphicsContext> LookAndFeel::createGraphicsContext(const juce::Image& _imageToRenderOn, juce::Point<int> _origin, const juce::RectangleList<int>& _initialClip)
	{
		m_currentImage = _imageToRenderOn;
		return LookAndFeel_V4::createGraphicsContext(_imageToRenderOn, _origin, _initialClip);
	}
}

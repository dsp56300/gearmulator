#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace juceRmlUi
{
	// The only purpose of this LookAndFeel subclass is to capture the Image that JUCE uses for rendering. The software renderer uses it as a target
	class LookAndFeel : public juce::LookAndFeel_V4
	{
		std::unique_ptr<juce::LowLevelGraphicsContext> createGraphicsContext(const juce::Image& _imageToRenderOn, juce::Point<int> _origin, const juce::RectangleList<int>& _initialClip) override;

	public:
		juce::Image& getCurrentImage() noexcept { return m_currentImage; }

	private:
		juce::Image m_currentImage;
	};
}

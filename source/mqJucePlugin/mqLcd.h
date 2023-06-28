#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "mqLcdBase.h"

class MqLcd final : juce::Button, public MqLcdBase, juce::Timer
{
public:
	explicit MqLcd(juce::Component& _parent);
	~MqLcd() override;

	void setText(const std::array<uint8_t, 40> &_text) override;
	void setCgRam(std::array<uint8_t, 64> &_data) override;

private:
	void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {}
	void paint(juce::Graphics& _g) override;
	juce::Path createPath(uint8_t _character) const;
	void onClicked();
	void timerCallback() override;

private:
	std::array<juce::Path, 256> m_characterPaths;

	const float m_scaleW;
	const float m_scaleH;

	std::array<uint8_t, 40> m_overrideText{0};
	std::array<uint8_t, 40> m_text{' '};
	std::array<std::array<uint8_t, 8>, 8> m_cgData{0};
	uint32_t m_charBgColor = 0;
};

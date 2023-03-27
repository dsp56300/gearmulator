#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class MqLcdBase
{
public:
	virtual ~MqLcdBase() = default;

	virtual void setText(const std::array<uint8_t, 40>& _text) = 0;
	virtual void setCgRam(std::array<uint8_t, 64>& _data) = 0;
};

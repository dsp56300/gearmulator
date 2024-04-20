#pragma once

#include "mqLcdBase.h"

namespace juce
{
	class Label;
}

class MqLcdText final : public MqLcdBase
{
public:
	MqLcdText(juce::Label& _lineA, juce::Label& _lineB);

	void setText(const std::array<uint8_t,40>& _text) override;
	void setCgRam(std::array<uint8_t, 64>& _data) override {}

private:
	juce::Label& m_lineA;
	juce::Label& m_lineB;
};

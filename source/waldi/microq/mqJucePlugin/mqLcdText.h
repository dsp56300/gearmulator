#pragma once

#include "mqLcdBase.h"

namespace Rml
{
	class Element;
}

namespace juce
{
	class Label;
}

class MqLcdText final : public MqLcdBase
{
public:
	MqLcdText(Rml::Element& _lineA, Rml::Element& _lineB);

	void setText(const std::array<uint8_t,40>& _text) override;
	void setCgRam(std::array<uint8_t, 64>& _data) override {}

private:
	Rml::Element& m_lineA;
	Rml::Element& m_lineB;
};

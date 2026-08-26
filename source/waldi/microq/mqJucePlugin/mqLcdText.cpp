#include "mqLcdText.h"

#include "RmlUi/Core/Element.h"

MqLcdText::MqLcdText(Rml::Element& _lineA, Rml::Element& _lineB) : m_lineA(_lineA), m_lineB(_lineB)
{
//	m_lineA.setJustificationType(juce::Justification::centredLeft);
//	m_lineB.setJustificationType(juce::Justification::centredLeft);
}

void MqLcdText::setText(const std::array<uint8_t, 40>& _text)
{
	std::array<char, 40> d;

	for (size_t i = 0; i < 40; ++i)
	{
		if (_text[i] < 32)
			d[i] = '?';
		else
			d[i] = static_cast<char>(_text[i]);
	}

	const std::string lineA(d.data(), 20);
	const std::string lineB(&d[20], 20);

//	LOG("LCD CONTENT:\n'" << lineA << "'\n'" << lineB << "'");

	m_lineA.SetInnerRML(Rml::StringUtilities::EncodeRml(lineA));
	m_lineB.SetInnerRML(Rml::StringUtilities::EncodeRml(lineB));
}

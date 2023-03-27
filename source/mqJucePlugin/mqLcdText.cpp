#include "mqLcdText.h"

MqLcdText::MqLcdText(juce::Label &_lineA, juce::Label &_lineB) : m_lineA(_lineA), m_lineB(_lineB)
{
	m_lineA.setJustificationType(juce::Justification::centredLeft);
	m_lineB.setJustificationType(juce::Justification::centredLeft);
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

	// nasty but works: prefix/postfix with byte 1 to prevent juce trimming the text
	m_lineA.setText("\1" + lineA + "\1", juce::dontSendNotification);
	m_lineB.setText("\1" + lineB + "\1", juce::dontSendNotification);
}

#include "textEditorStyle.h"

namespace genericUI
{
	void TextEditorStyle::apply(juce::TextEditor& _target) const
	{
		_target.setColour(juce::TextEditor::ColourIds::backgroundColourId, m_bgColor);
		_target.setColour(juce::TextEditor::ColourIds::outlineColourId, m_outlineColor);
		_target.setColour(juce::TextEditor::ColourIds::textColourId, m_color);
		_target.setColour(juce::TextEditor::ColourIds::focusedOutlineColourId, m_outlineColor);
		_target.setColour(juce::TextEditor::ColourIds::highlightedTextColourId, m_color);

		_target.setTextToShowWhenEmpty(m_text, m_color.withAlpha(0.5f));
	}
}

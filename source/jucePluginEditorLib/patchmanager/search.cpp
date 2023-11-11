#include "search.h"

#include "dsp56kEmu/logging.h"

namespace jucePluginEditorLib::patchManager
{
	Search::Search()
	{
		addListener(this);
	}

	void Search::textEditorTextChanged(juce::TextEditor& _textEditor)
	{
		setText(_textEditor.getText().toStdString());
	}

	std::string Search::lowercase(const std::string& _s)
	{
		auto t = _s;
		std::transform(t.begin(), t.end(), t.begin(), tolower);
		return t;
	}

	void Search::onTextChanged(const std::string& _text)
	{
	}

	void Search::paint(juce::Graphics& g)
	{
		TextEditor::paint(g);
	}

	void Search::setText(const std::string& _text)
	{
		const auto t = lowercase(_text);

		if (m_text == t)
			return;

		m_text = t;
		onTextChanged(t);
	}
}

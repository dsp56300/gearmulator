#include "search.h"

#include "dsp56kEmu/logging.h"

namespace jucePluginEditorLib::patchManager
{
	Search::Search()
	{
		setEditable(true, true);
		addListener(this);
	}

	void Search::textWasChanged()
	{
		setText(getText().toStdString());
	}

	void Search::textWasEdited()
	{
		setText(getText().toStdString());
	}

	void Search::labelTextChanged(Label* _label)
	{
		setText(getText().toStdString());
	}

	void Search::editorShown(Label* _label, juce::TextEditor& _editor)
	{
		Listener::editorShown(_label, _editor);
	}

	void Search::editorHidden(Label* _label, juce::TextEditor& _editor)
	{
		Listener::editorHidden(_label, _editor);
	}

	void Search::textEditorTextChanged(juce::TextEditor& _textEditor)
	{
		setText(_textEditor.getText().toStdString());
	}

	void Search::onTextChanged(const std::string& _text)
	{
	}

	void Search::setText(const std::string& _text)
	{
		if (m_text == _text)
			return;

		m_text = _text;
		onTextChanged(_text);
	}
}

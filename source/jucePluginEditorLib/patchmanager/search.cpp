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
		LOG("textWasChanged");
	}

	void Search::textWasEdited()
	{
		LOG("textWasEdited");
	}

	void Search::labelTextChanged(Label* _label)
	{
		LOG("labeltextChanged");
	}

	void Search::textEditorTextChanged(juce::TextEditor& _textEditor)
	{
		LOG("textEditTextChanged: " << _textEditor.getText().toStdString());
	}
}

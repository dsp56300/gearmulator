#include "editable.h"

namespace jucePluginEditorLib::patchManager
{
	Editable::~Editable()
	{
		destroyEditorLabel();
	}

	bool Editable::beginEdit(juce::Component* _parent, const juce::Rectangle<int>& _position, const std::string& _initialText, FinishedEditingCallback&& _callback)
	{
		if (m_editorLabel)
			return false;

		m_editorInitialText = _initialText;
		m_editorLabel = new juce::Label({}, _initialText);

		const auto pos = _position;

		m_editorLabel->setTopLeftPosition(pos.getTopLeft());
		m_editorLabel->setSize(pos.getWidth(), pos.getHeight());

		m_editorLabel->setEditable(true, true, true);
		m_editorLabel->setColour(juce::Label::backgroundColourId, juce::Colour(0xff333333));

		m_editorLabel->addListener(this);

		_parent->addAndMakeVisible(m_editorLabel);

		m_editorLabel->showEditor();

		m_finishedEditingCallback = std::move(_callback);

		return true;
	}

	void Editable::editorHidden(juce::Label* _label, juce::TextEditor& _textEditor)
	{
		if (m_editorLabel)
		{
			const auto text = m_editorLabel->getText().toStdString();
			if(text != m_editorInitialText)
				m_finishedEditingCallback(true, text);
			destroyEditorLabel();
		}
		Listener::editorHidden(_label, _textEditor);
	}

	void Editable::labelTextChanged(juce::Label* _label)
	{
	}

	void Editable::destroyEditorLabel()
	{
		if (!m_editorLabel)
			return;

		m_editorLabel->getParentComponent()->removeChildComponent(m_editorLabel);
		delete m_editorLabel;
		m_editorLabel = nullptr;

		m_finishedEditingCallback = {};
	}
}

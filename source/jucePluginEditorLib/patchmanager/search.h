#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib::patchManager
{
	class Search : public juce::TextEditor, juce::TextEditor::Listener
	{
	public:
		Search();

		void textEditorTextChanged(juce::TextEditor&) override;

		static std::string lowercase(const std::string& _s);

		virtual void onTextChanged(const std::string& _text);

		void paint(juce::Graphics& g) override;
	private:
		void setText(const std::string& _text);

		std::string m_text;
	};
}

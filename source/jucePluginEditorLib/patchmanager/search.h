#pragma once

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

namespace jucePluginEditorLib::patchManager
{
	class Search : public juce::Label, juce::Label::Listener
	{
	public:
		Search();

		void textWasChanged() override;
		void textWasEdited() override;
		void labelTextChanged(Label* _label) override;
		void editorShown(Label* _label, juce::TextEditor& _editor) override;
		void editorHidden(Label* _label, juce::TextEditor& _editor) override;
		void textEditorTextChanged(juce::TextEditor&) override;

		virtual void onTextChanged(const std::string& _text);

	private:
		void setText(const std::string& _text);

		std::string m_text;
	};
}

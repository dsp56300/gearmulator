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
		void textEditorTextChanged(juce::TextEditor&) override;
	};
}

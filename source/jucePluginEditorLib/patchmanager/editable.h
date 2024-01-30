#pragma once

#include <functional>
#include <string>

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib::patchManager
{
	class Editable : juce::Label::Listener
	{
	public:
		using FinishedEditingCallback = std::function<void(bool, const std::string&)>;

		~Editable() override;

	protected:
		bool beginEdit(juce::Component* _parent, const juce::Rectangle<int>& _position, const std::string& _initialText, FinishedEditingCallback&& _callback);

		// juce::Label::Listener
		void editorHidden(juce::Label*, juce::TextEditor&) override;
		void labelTextChanged(juce::Label* _label) override;

	private:
		void destroyEditorLabel();

		FinishedEditingCallback m_finishedEditingCallback;
		juce::Label* m_editorLabel = nullptr;
		std::string m_editorInitialText;
	};
}

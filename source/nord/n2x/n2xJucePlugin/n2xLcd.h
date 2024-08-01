#pragma once

#include <string>

#include "juce_events/juce_events.h"

namespace juce
{
	class Label;
}

namespace n2xJucePlugin
{
	class Editor;

	class Lcd : juce::Timer
	{
	public:
		enum class AnimState
		{
			Start,
			Scroll,
			End
		};

		explicit Lcd(Editor& _editor);

		void setText(const std::string& _key);

		void timerCallback() override;

	private:
		void setClippedText(const std::string& _text);
		bool updateClippedText(const std::string& _text, uint32_t _offset);
		void startAnim();
		void onTextChanged();

		Editor& m_editor;
		juce::Label* m_label;
		std::string m_text;
		std::string m_clippedText;
		uint32_t m_currentOffset = 0;
		AnimState m_animState = AnimState::Start;
	};
}

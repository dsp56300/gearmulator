#pragma once

#include <string>

#include "jucePluginLib/event.h"

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

		void setText(const std::string& _text);

		void timerCallback() override;

		void updatePatchName();

		void setOverrideText(const std::string& _text);

	private:
		void setClippedText(const std::string& _text);
		static std::string substring(const std::string& _text, uint32_t _offset, uint32_t _len);
		bool updateClippedText(const std::string& _text, uint32_t _offset);
		void startAnim();
		void onTextChanged();
		void onProgramChanged();
		const std::string& getCurrentText() const;

		Editor& m_editor;
		juce::Label* m_label;
		std::string m_text;
		std::string m_clippedText;
		uint32_t m_currentOffset = 0;
		AnimState m_animState = AnimState::Start;

		std::string m_overrideText;

		pluginLib::EventListener<> m_onProgramChanged;
		pluginLib::EventListener<uint8_t> m_onPartChanged;
	};
}

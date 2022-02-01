#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace Buttons
{
	class Button1 : public juce::DrawableButton
	{
	public:
		Button1();
		static constexpr auto kWidth = 36;
		static constexpr auto kHeight = 136/2;
	};

	class Button2 : public juce::DrawableButton
	{
	public:
		Button2();
		static constexpr auto kWidth = 69;
		static constexpr auto kHeight = 134/2;
	};

	class Button3 : public juce::DrawableButton
	{
	public:
		Button3();
		static constexpr auto kWidth = 69;
		static constexpr auto kHeight = 134/2;
	};

	class Button4 : public juce::DrawableButton
	{
	public:
		Button4();
		static constexpr auto kWidth = 68;
		static constexpr auto kHeight = 72/2;
	};

	class ButtonMenu: public juce::DrawableButton
	{
	public:
		static constexpr auto kWidth = 172;
		static constexpr auto kHeight = 51;
		ButtonMenu();
	};

	class PresetButtonLeft: public juce::DrawableButton
	{
	public:
		static constexpr auto kWidth = 33;
		static constexpr auto kHeight = 51;
		PresetButtonLeft();
	};

	class PresetButtonRight: public juce::DrawableButton
	{
	public:
		static constexpr auto kWidth = 33;
		static constexpr auto kHeight = 51;
		PresetButtonRight();
	};

	class PresetButtonDown: public juce::DrawableButton
	{
	public:
		static constexpr auto kWidth = 33;
		static constexpr auto kHeight = 51;
		PresetButtonDown();
	};
	
	class OptionButtonLoadBank: public juce::DrawableButton
	{
	public:
		static constexpr auto kWidth = 172;
		static constexpr auto kHeight = 50;
		OptionButtonLoadBank();
	};

	class OptionButtonSavePreset: public juce::DrawableButton
	{
	public:
		static constexpr auto kWidth = 172;
		static constexpr auto kHeight = 50;
		OptionButtonSavePreset();
	};

} // namespace Buttons

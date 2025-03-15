#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	namespace settings
	{
		class Style : public juce::LookAndFeel_V4
		{
		public:
			void apply(juce::Component* _component);

			juce::Font getTextButtonFont(juce::TextButton&, int _buttonHeight) override;
			juce::Font getLabelFont(juce::Label&) override;
		};

		class StyleHeadline1 final : public Style
		{
			juce::Font getLabelFont(juce::Label&) override;
			void drawLabel(juce::Graphics&, juce::Label&) override;
		};

		class StyleHeadline2 final : public Style
		{
			juce::Font getLabelFont(juce::Label&) override;
		};

		Style& getStyle();
		Style& getStyleHeadline1();
		Style& getStyleHeadline2();
	}
}

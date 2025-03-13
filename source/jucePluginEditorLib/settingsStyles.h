#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	namespace settings
	{
		class Style final : public juce::LookAndFeel_V4
		{
		public:
			void apply(juce::Component* _component);

			juce::Font getTextButtonFont(juce::TextButton&, int _buttonHeight) override;
			juce::Font getLabelFont(juce::Label&) override;
		};

		Style& getStyle();
	}
}

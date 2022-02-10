#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

constexpr auto comboBoxHeight = 38;
constexpr auto comboBoxWidth = 126;

constexpr auto comboBox2Height = 52;
constexpr auto comboBox2Width = 74;

constexpr auto comboBox3Height = 52;
constexpr auto comboBox3Width = 186;

constexpr auto kPanelWidth = 2501;
constexpr auto kPanelHeight = 1152;

constexpr auto comboBoxXMargin = 5;

namespace Virus
{
    const juce::Font getCustomFont();

    class CustomLAF : public juce::LookAndFeel_V4
    {
    public:
        void drawLabel(juce::Graphics& g, juce::Label& l) override
        {
            // g.fillAll (label.findColour (Label::backgroundColourId));
            g.setColour (l.findColour (juce::Label::backgroundColourId));
            g.fillRoundedRectangle (l.getLocalBounds().toFloat(), 5);
   
            // g.drawRect (label.getLocalBounds());
            g.drawRoundedRectangle (l.getLocalBounds().toFloat(), 10, 1); 
        }
        juce::Font getLabelFont(juce::Label& label) override
        {
			juce::Font fFont(getCustomFont().getTypeface());
            fFont.setHeight(20.f);
            return fFont;
        }
    };

    class LookAndFeelSmallButton : public juce::LookAndFeel_V4/*, juce::Slider*/
    {
    public:
        LookAndFeelSmallButton();

        static constexpr auto kKnobSize = 55;
        static const char *KnobStyleProp;

        void drawRotarySlider(juce::Graphics &, int x, int y, int width, int height, float sliderPosProportional,
                              float rotaryStartAngle, float rotaryEndAngle, juce::Slider &sSlider) override;


    private:
        std::unique_ptr<juce::Drawable> m_genKnob;
        juce::NormalisableRange<float> m_knobImageSteps;
    };


    class LookAndFeel : public juce::LookAndFeel_V4/*, juce::Slider*/
    {
    public:
        LookAndFeel();

        static constexpr auto kKnobSize = 75;
        static const char *KnobStyleProp;

        void drawRotarySlider(juce::Graphics &, int x, int y, int width, int height, float sliderPosProportional,
                              float rotaryStartAngle, float rotaryEndAngle, juce::Slider &sSlider) override;


		juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
        {
            return getCustomFont().getTypeface();
        }

        void drawComboBox (juce::Graphics& gGrafik, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override;

        juce::Font getComboBoxFont (juce::ComboBox& ) override
        {
			juce::Font fFont(getCustomFont().getTypeface());
            fFont.setHeight(20.f);
            return fFont;
        }

        juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override
        {
			juce::Font fFont(getCustomFont().getTypeface());
            fFont.setHeight(20.f);
            return fFont;
        }

	    juce::Font getPopupMenuFont() override
	    { 
			juce::Font fFont(getCustomFont().getTypeface());
            fFont.setHeight(20.f);
            return fFont;
	    }

    private:
        std::unique_ptr<juce::Drawable> m_genKnob;
        juce::NormalisableRange<float> m_knobImageSteps;
    };
	
    class LookAndFeelPatchBrowser : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeelPatchBrowser();

        juce::Font getComboBoxFont (juce::ComboBox& /*box*/) override
        {
            return getCommonMenuFont();
        }

	    juce::Font getPopupMenuFont() override 
	    { 
		    return getCommonMenuFont(); 
	    }

    private:
		juce::Font getCommonMenuFont() 
	    { 
		    return juce::Font("Arial", "Normal", 13.f);
	    }
    };

    class LookAndFeelButtons : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeelButtons();

        juce::Font getComboBoxFont (juce::ComboBox& /*box*/) override
        {
            return getCommonMenuFont();
        }

	    juce::Font getPopupMenuFont() override 
	    { 
		    return getCommonMenuFont(); 
	    }

        void drawComboBox (juce::Graphics& gGrafik, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override;

    private:
		juce::Font getCommonMenuFont() 
	    { 
		    return juce::Font("Arial", "Normal", 23.f);
	    }
    };

} // namespace Virus

#include "Virus_LookAndFeel.h"
#include "BinaryData.h"

using namespace juce;

namespace Virus
{
    const juce::Font getCustomFont()
    {
        static auto typefaceDigiFont = juce::Typeface::createSystemTypefaceFor(BinaryData::Digital, BinaryData::DigitalSize);  
        return juce::Font (typefaceDigiFont);
    }

	//LookAndFeel
    LookAndFeelSmallButton::LookAndFeelSmallButton()
	{
		// setup knobs...
		m_genKnob = Drawable::createFromImageData(BinaryData::knob_2_128_png, BinaryData::knob_2_128_pngSize);

		m_knobImageSteps.start = 1;
		m_knobImageSteps.end = 127;
	}

    void LookAndFeelSmallButton::drawRotarySlider(Graphics &g, int x, int y, int width, int height, float sliderPosProportional,
									   float rotaryStartAngle, float rotaryEndAngle, Slider &s)
	{
		Drawable *knob = m_genKnob.get();

		int step;
		(s.isEnabled())?step=roundToInt(m_knobImageSteps.convertFrom0to1(sliderPosProportional)):step=0;

		knob->setOriginWithOriginalSize({0.0f, -float(kKnobSize) * step});
		
		// this won't support scaling!
		knob->drawAt(g, x, y, 1.0f);
	}

	LookAndFeel::LookAndFeel()
	{
		// setup knobs...
		m_genKnob = Drawable::createFromImageData(BinaryData::knob_1_128_png, BinaryData::knob_1_128_pngSize);

		m_knobImageSteps.start = 1;
		m_knobImageSteps.end = 127;
	}
 
	void LookAndFeel::drawRotarySlider(Graphics &g, int x, int y, int width, int height, float sliderPosProportional,
									   float rotaryStartAngle, float rotaryEndAngle, Slider &s)
	{	
		Drawable *knob = m_genKnob.get();

		int step;
		(s.isEnabled())?step=roundToInt(m_knobImageSteps.convertFrom0to1(sliderPosProportional)):step=0;

		knob->setOriginWithOriginalSize({0.0f, -float(kKnobSize) * step});
		
		// this won't support scaling!
		knob->drawAt(g, x, y, 1.0f);
	}

    void LookAndFeel::drawComboBox (juce::Graphics& gGrafik, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box)
    {
        //gGrafik.setOpacity(0);
        //box.setColour(juce::ComboBox::textColourId, juce::Colours::red);
    };

	//LookAndFeelPatchBrowser
	LookAndFeelPatchBrowser::LookAndFeelPatchBrowser()
	{
	}

	//LookAndFeelButtons
	LookAndFeelButtons::LookAndFeelButtons()
	{
	}

    void LookAndFeelButtons::drawComboBox (juce::Graphics& gGrafik, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box)
    {
        gGrafik.setOpacity(0);
    };

} // namespace Virus

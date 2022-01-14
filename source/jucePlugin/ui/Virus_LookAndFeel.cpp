#include "Virus_LookAndFeel.h"
#include "BinaryData.h"

using namespace juce;

namespace Virus
{

    const char *LookAndFeel::KnobStyleProp = "knob_style";

    LookAndFeel::LookAndFeel()
    {
        // setup knobs...
        m_genKnob = Drawable::createFromImageData(BinaryData::Gen_70x70_100_png, BinaryData::Gen_70x70_100_pngSize);
        m_genPol =
            Drawable::createFromImageData(BinaryData::Gen_pol_70x70_100_png, BinaryData::Gen_pol_70x70_100_pngSize);
        m_genBlue =
            Drawable::createFromImageData(BinaryData::GenBlue_70x70_100_png, BinaryData::GenBlue_70x70_100_pngSize);
        m_genRed =
            Drawable::createFromImageData(BinaryData::GenRed_70x70_100_png, BinaryData::GenRed_70x70_100_pngSize);
        m_multi = Drawable::createFromImageData(BinaryData::multi_18x18_100_png, BinaryData::multi_18x18_100_pngSize);

        m_knobImageSteps.start = 0;
        m_knobImageSteps.end = 99;
    }

    void LookAndFeel::drawRotarySlider(Graphics &g, int x, int y, int width, int height, float sliderPosProportional,
                                       float rotaryStartAngle, float rotaryEndAngle, Slider &s)
    {
        Drawable *knob;
        switch (static_cast<int>(s.getProperties().getWithDefault(KnobStyleProp, KnobStyle::GENERIC)))
        {
        case KnobStyle::GENERIC_POL:
            knob = m_genPol.get();
            break;
        case KnobStyle::GENERIC_RED:
            knob = m_genRed.get();
            break;
        case KnobStyle::GENERIC_BLUE:
            knob = m_genBlue.get();
            break;
        case KnobStyle::GENERIC_MULTI:
            knob = m_multi.get();
            break;
        case KnobStyle::GENERIC:
        default:
            knob = m_genKnob.get();
        }

        {
            // debug
            //        g.fillAll (Colours::pink.withAlpha (0.4f));
            const auto step = roundToInt(m_knobImageSteps.convertFrom0to1(sliderPosProportional));
            // take relevant pos
            if (knob == m_multi.get()) {
                knob->setOriginWithOriginalSize({0.0f, -18.0f * step});
            }
            else {
                knob->setOriginWithOriginalSize({0.0f, -70.0f * step});
            }

        }
        // this won't support scaling!
        knob->drawAt(g, x, y, 1.0f);
    }

void LookAndFeel::drawComboBox (Graphics& g, int width, int height, bool isButtonDown,
                   int buttonX, int buttonY, int buttonW, int buttonH,
                   ComboBox&)
{
    // panels draws combo box... so it's invisible :)
}
void LookAndFeel::drawButtonBackground(juce::Graphics &, juce::Button &, const juce::Colour &backgroundColour, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{

}

} // namespace Virus

#include "Virus_LookAndFeel.h"

constexpr auto knobSize = Virus::LookAndFeel::kKnobSize;
constexpr auto comboBoxHeight = 17;

static void setupBackground(juce::Component &parent, std::unique_ptr<juce::Drawable> &bg, const void *data,
                            const size_t numBytes)
{
    bg = juce::Drawable::createFromImageData(data, numBytes);
    parent.addAndMakeVisible(bg.get());
    bg->setBounds(bg->getDrawableBounds().toNearestIntEdges());
}

static void setupRotary(juce::Component &parent, juce::Slider &slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    parent.addAndMakeVisible(slider);
}

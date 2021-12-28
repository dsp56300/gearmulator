#include "Virus_ArpEditor.h"
#include "BinaryData.h"
#include "Ui_Utils.h"

constexpr auto comboBoxWidth = 84;

using namespace juce;

ArpEditor::ArpEditor()
{
    setupBackground(*this, m_background, BinaryData::bg_arp_1018x620_png, BinaryData::bg_arp_1018x620_pngSize);
    setBounds(m_background->getDrawableBounds().toNearestIntEdges());

    m_velocityAmount.setBounds(23, 28, 439, 238);
    addAndMakeVisible(m_velocityAmount);
    m_inputs.setBounds(23, m_velocityAmount.getBottom() + 2, 439, 118);
    addAndMakeVisible(m_inputs);
    m_arp.setBounds(m_velocityAmount.getRight() + 2, 28, 552, 120);
    addAndMakeVisible(m_arp);
    m_softKnobs.setBounds(m_arp.getX(), m_arp.getBottom() + 2, 552, 116);
    addAndMakeVisible(m_softKnobs);
    m_patchSettings.setBounds(m_softKnobs.getX(), m_softKnobs.getBottom() + 2, 552, 194);
    addAndMakeVisible(m_patchSettings);
}

ArpEditor::VelocityAmount::VelocityAmount()
{
    constexpr auto y = 19;
    for (auto *s : {&m_osc1Shape, &m_filter1Freq, &m_filter1Res, &m_pulseWidth, &m_volume, &m_panorama, &m_osc2Shape,
                    &m_filter2Freq, &m_filter2Res, &m_fmAmount})
        setupRotary(*this, *s);
    m_osc1Shape.setBounds(31, y, knobSize, knobSize);
    m_filter1Freq.setBounds(m_osc1Shape.getRight() - 7, y, knobSize, knobSize);
    m_filter1Res.setBounds(m_filter1Freq.getRight() - 8, y, knobSize, knobSize);
    m_pulseWidth.setBounds(m_filter1Res.getRight() - 7, y, knobSize, knobSize);
    m_volume.setBounds(m_pulseWidth.getRight() - 3, y, knobSize, knobSize);
    m_panorama.setBounds(m_volume.getRight() - 9, y, knobSize, knobSize);

    const auto y2 = m_osc1Shape.getBottom() + y;
    m_osc2Shape.setBounds(31, y2, knobSize, knobSize);
    m_filter2Freq.setBounds(m_osc1Shape.getRight() - 7, y2, knobSize, knobSize);
    m_filter2Res.setBounds(m_filter1Freq.getRight() - 8, y2, knobSize, knobSize);
    m_fmAmount.setBounds(m_filter1Res.getRight() - 7, y2, knobSize, knobSize);
}

ArpEditor::Inputs::Inputs()
{
    addAndMakeVisible(m_inputMode);
    m_inputMode.setBounds(43, 38, comboBoxWidth, comboBoxHeight);
    addAndMakeVisible(m_inputSelect);
    m_inputSelect.setBounds(145, 38, comboBoxWidth, comboBoxHeight);
}

ArpEditor::Arpeggiator::Arpeggiator()
{
    constexpr auto y = 18;
    for (auto *s : {&m_globalTempo, &m_noteLength, &m_noteSwing})
        setupRotary(*this, *s);
    m_globalTempo.setBounds(341, y, knobSize, knobSize);
    m_noteLength.setBounds(m_globalTempo.getRight() - 8, y, knobSize, knobSize);
    m_noteSwing.setBounds(m_noteLength.getRight() - 7, y, knobSize, knobSize);

    for (auto *c : {&m_mode, &m_pattern, &m_octaveRange, &m_resolution})
        addAndMakeVisible(c);

    constexpr auto comboBoxWidth = 90;
    constexpr auto comboBoxHeight = 15;
    constexpr auto comboTopY = 35;

    m_mode.setBounds(39, 40, 52, 38);
    m_pattern.setBounds(114, comboTopY, comboBoxWidth, comboBoxHeight);
    m_resolution.setBounds(220, comboTopY, comboBoxWidth, comboBoxHeight);
    m_octaveRange.setBounds(m_pattern.getBounds().translated(0, comboBoxHeight + 18));

    m_arpHold.setButtonText("On");
    addAndMakeVisible(m_arpHold);
    m_arpHold.setBounds(220, m_octaveRange.getY(), 32, comboBoxHeight);
}

ArpEditor::SoftKnobs::SoftKnobs()
{
    auto distance = 105;
    for (auto i = 0; i < 2; i++)
    {
        addAndMakeVisible(m_funcAs[i]);
        m_funcAs[i].setBounds(i == 0 ? 18 : 338, 42, comboBoxWidth, comboBoxHeight);
        addAndMakeVisible(m_name[i]);
        m_name[i].setBounds(m_funcAs[i].getX() + distance, 42, comboBoxWidth, comboBoxHeight);
    }
}

ArpEditor::PatchSettings::PatchSettings()
{
    constexpr auto y = 18;
    for (auto *s : {&m_patchVolume, &m_panning, &m_outputBalance, &m_transpose})
        setupRotary(*this, *s);
    m_patchVolume.setBounds(101, y, knobSize, knobSize);
    m_panning.setBounds(m_patchVolume.getRight() - 9, y, knobSize, knobSize);
    const auto y2 = m_patchVolume.getBottom() + 9;
    m_outputBalance.setBounds(m_patchVolume.getX(), y2, knobSize, knobSize);
    m_transpose.setBounds(m_panning.getX(), y2, knobSize, knobSize);

    for (auto *cb :
         {&m_keyMode, &m_secondaryOutput, &m_bendUp, &m_bendDown, &m_bendScale, &m_smoothMode, &m_cat1, &m_cat2})
        addAndMakeVisible(cb);


    constexpr auto yDist = 50;
    m_keyMode.setBounds(18, 42, comboBoxWidth, comboBoxHeight);
    m_secondaryOutput.setBounds(18, 122, comboBoxWidth, comboBoxHeight);
    constexpr auto x1 = 338;
    constexpr auto x2 = 444;
    m_bendUp.setBounds(x1, 42, comboBoxWidth, comboBoxHeight);
    m_bendScale.setBounds(x1, 42 + yDist, comboBoxWidth, comboBoxHeight);
    m_cat1.setBounds(x1, m_bendScale.getY() + yDist - 1, comboBoxWidth, comboBoxHeight);
    m_bendDown.setBounds(x2, 42, comboBoxWidth, comboBoxHeight);
    m_smoothMode.setBounds(x2, m_bendScale.getY(), comboBoxWidth, comboBoxHeight);
    m_cat2.setBounds(x2, m_cat1.getY(), comboBoxWidth, comboBoxHeight);
}

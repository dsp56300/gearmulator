#include "Virus_FxEditor.h"
#include "BinaryData.h"
#include "Ui_Utils.h"

using namespace juce;

constexpr auto comboBoxWidth = 84;

FxEditor::FxEditor()
{
    setupBackground(*this, m_background, BinaryData::bg_fx_1018x620_png, BinaryData::bg_fx_1018x620_pngSize);
    setBounds(m_background->getDrawableBounds().toNearestIntEdges());

    m_dist.setBounds(23, 28, 273, 116);
    addAndMakeVisible(m_dist);
    m_analogBoost.setBounds(m_dist.getRight() + 2, 28, 235, 116);
    addAndMakeVisible(m_analogBoost);
    m_phaser.setBounds(m_dist.getX(), m_dist.getBottom() + 2, 510, 116);
    addAndMakeVisible(m_phaser);
    m_chorus.setBounds(m_phaser.getBounds().withY(m_phaser.getBottom() + 2));
    addAndMakeVisible(m_chorus);
    m_eq.setBounds(23, m_chorus.getBottom() + 2, 510, 109);
    addAndMakeVisible(m_eq);
    m_envFollow.setBounds(23, m_eq.getBottom() + 2, 510 - 174 - 2, 103);
    addAndMakeVisible(m_envFollow);
    m_punch.setBounds(m_envFollow.getRight() + 2, m_envFollow.getY(), 174, 103);
    addAndMakeVisible(m_punch);
    m_delayReverb.setBounds(m_phaser.getRight() + 2, m_dist.getY(), 481, m_phaser.getHeight() * 2);
    addAndMakeVisible(m_delayReverb);
    m_vocoder.setBounds(m_delayReverb.getBounds().withY(m_delayReverb.getBottom() + 2).withHeight(304));
    addAndMakeVisible(m_vocoder);
}

FxEditor::Distortion::Distortion()
{
    setupRotary(*this, m_intensity);
    m_intensity.setBounds(101, 18, knobSize, knobSize);
    addAndMakeVisible(m_curve);
    m_curve.setBounds(17, 42, comboBoxWidth, comboBoxHeight);
}

FxEditor::AnalogBoost::AnalogBoost()
{
    for (auto *s : {&m_boost, &m_tune})
        setupRotary(*this, *s);
    m_boost.setBounds(16, 18, knobSize, knobSize);
    m_tune.setBounds(m_boost.getBounds().withX(m_boost.getRight() - 4));
}

FxEditor::Phaser::Phaser()
{
    constexpr auto y = 16;
    for (auto *s : {&m_rate, &m_freq, &m_depth, &m_feedback, &m_spread, &m_mix})
        setupRotary(*this, *s);
    m_rate.setBounds(100, 16, knobSize, knobSize);
    m_freq.setBounds(m_rate.getRight() - 8, y, knobSize, knobSize);
    m_depth.setBounds(m_freq.getRight() - 7, y, knobSize, knobSize);
    m_feedback.setBounds(m_depth.getRight() - 5, y, knobSize, knobSize);
    m_spread.setBounds(m_feedback.getRight() - 5, y, knobSize, knobSize);
    m_mix.getProperties().set(Virus::LookAndFeel::KnobStyleProp, Virus::LookAndFeel::KnobStyle::GENERIC_RED);
    m_mix.setBounds(m_spread.getRight() - 7, y, knobSize, knobSize);
    addAndMakeVisible(m_stages);
    m_stages.setBounds(17, 41, comboBoxWidth, comboBoxHeight);
}

FxEditor::Chorus::Chorus()
{
    constexpr auto y = 18;
    for (auto *s : {&m_rate, &m_depth, &m_feedback, &m_delay, &m_mix})
        setupRotary(*this, *s);
    m_rate.setBounds(101, y, knobSize, knobSize);
    m_depth.setBounds(m_rate.getRight() - 8, y, knobSize, knobSize);
    m_feedback.setBounds(m_depth.getRight() - 8, y, knobSize, knobSize);
    m_delay.setBounds(m_feedback.getRight() - 6, y, knobSize, knobSize);
    m_mix.getProperties().set(Virus::LookAndFeel::KnobStyleProp, Virus::LookAndFeel::KnobStyle::GENERIC_RED);
    m_mix.setBounds(m_delay.getRight() - 4, y, knobSize, knobSize);
    addAndMakeVisible(m_lfoShape);
    m_lfoShape.setBounds(17, 42, comboBoxWidth, comboBoxHeight);
}

FxEditor::Equalizer::Equalizer()
{
    constexpr auto y = 18;
    for (auto *s : {&m_low_gain, &m_low_freq, &m_mid_gain, &m_mid_freq, &m_mid_q, &m_high_gain, &m_high_freq})
        setupRotary(*this, *s);
    m_low_gain.setBounds(37, y, knobSize, knobSize);
    m_low_freq.setBounds(m_low_gain.getRight() - 6, y, knobSize, knobSize);
    m_mid_gain.setBounds(m_low_freq.getRight() - 8, y, knobSize, knobSize);
    m_mid_freq.setBounds(m_mid_gain.getRight() - 7, y, knobSize, knobSize);
    m_mid_q.setBounds(m_mid_freq.getRight() - 6, y, knobSize, knobSize);
    m_high_gain.setBounds(m_mid_q.getRight() - 6, y, knobSize, knobSize);
    m_high_freq.setBounds(m_high_gain.getRight() - 6, y, knobSize, knobSize);
}

FxEditor::EnvelopeFollower::EnvelopeFollower()
{
    constexpr auto y = 12;
    for (auto *s : {&m_gain, &m_attack, &m_release})
        setupRotary(*this, *s);
    m_gain.setBounds(101, y, knobSize, knobSize);
    m_attack.setBounds(m_gain.getRight() - 8, y, knobSize, knobSize);
    m_release.setBounds(m_attack.getRight() - 7, y, knobSize, knobSize);
    addAndMakeVisible(m_input);
    m_input.setBounds(17, 37, comboBoxWidth, comboBoxHeight);
}

FxEditor::Punch::Punch()
{
    setupRotary(*this, m_amount);
    m_amount.setBounds(19, 12, knobSize, knobSize);
}

FxEditor::DelayAndReverb::DelayAndReverb()
{
    constexpr auto y = 18;
    for (auto *s : {&m_time, &m_rate, &m_depth, &m_color, &m_feedback})
        setupRotary(*this, *s);
    m_time.setBounds(118, 18, knobSize, knobSize);
    m_rate.setBounds(m_time.getRight() - 8, y, knobSize, knobSize);
    m_depth.setBounds(m_rate.getRight() - 8, y, knobSize, knobSize);
    m_color.setBounds(m_depth.getRight() - 3, y, knobSize, knobSize);
    m_feedback.setBounds(m_color.getRight() - 3, y, knobSize, knobSize);

    addAndMakeVisible(m_fxMode);
    m_fxMode.setBounds(18, 42, comboBoxWidth, comboBoxHeight);

    m_sync.setBounds(0, 116 + 2, 481, 116);
    addAndMakeVisible(m_sync);
}

FxEditor::DelayAndReverb::Sync::Sync()
{
    setupRotary(*this, m_mix);
    m_mix.getProperties().set(Virus::LookAndFeel::KnobStyleProp, Virus::LookAndFeel::KnobStyle::GENERIC_RED);
    m_mix.setBounds(376, -2, knobSize, knobSize);

    addAndMakeVisible(m_clock);
    m_clock.setBounds(18, 22, comboBoxWidth, comboBoxHeight);
    addAndMakeVisible(m_lfoShape);
    m_lfoShape.setBounds(m_clock.getBounds().getRight() + 26, 22, comboBoxWidth, comboBoxHeight);
}

FxEditor::Vocoder::Vocoder() : m_link(true)
{
    constexpr auto y = 17;
    for (auto *s : {&m_sourceBalance, &m_spectralBalance, &m_bands, &m_attack, &m_release})
        setupRotary(*this, *s);
    m_sourceBalance.setBounds(117, 17, knobSize, knobSize);
    m_spectralBalance.setBounds(m_sourceBalance.getRight() - 8, y, knobSize, knobSize);
    m_bands.setBounds(m_spectralBalance.getRight() - 7, y, knobSize, knobSize);
    m_attack.setBounds(m_bands.getRight() - 2, y, knobSize, knobSize);
    m_release.setBounds(m_attack.getRight() - 3, y, knobSize, knobSize);

    m_carrier.setBounds(105, 125, 328, 80);
    addAndMakeVisible(m_carrier);
    m_modulator.setBounds(10, 218, 423, 81);
    addAndMakeVisible(m_modulator);

    addAndMakeVisible(m_link);
    m_link.setBounds(445, 195, 12, 36);
    addAndMakeVisible(m_mode);
    m_mode.setBounds(16, 43, comboBoxWidth, comboBoxHeight);
}

FxEditor::Vocoder::Carrier::Carrier()
{
    constexpr auto y = -4;
    for (auto *s : {&m_center_freq, &m_q_factor, &m_spread})
        setupRotary(*this, *s);
    m_center_freq.setBounds(12, -4, knobSize, knobSize);
    m_q_factor.setBounds(m_center_freq.getRight() - 8, y, knobSize, knobSize);
    m_spread.setBounds(m_q_factor.getRight() - 7, y, knobSize, knobSize);
}

FxEditor::Vocoder::Modulator::Modulator()
{
    constexpr auto y = -4;
    for (auto *s : {&m_freq_offset, &m_q_factor, &m_spread})
        setupRotary(*this, *s);
    m_freq_offset.setBounds(107, y, knobSize, knobSize);
    m_q_factor.setBounds(m_freq_offset.getRight() - 8, y, knobSize, knobSize);
    m_spread.setBounds(m_q_factor.getRight() - 7, y, knobSize, knobSize);
    addAndMakeVisible(m_modInput);
    m_modInput.setBounds(8, 23, comboBoxWidth, comboBoxHeight);
}

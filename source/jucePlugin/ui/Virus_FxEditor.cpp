#include "Virus_FxEditor.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "../VirusParameterBinding.h"
using namespace juce;

constexpr auto comboBoxWidth = 84;

FxEditor::FxEditor(VirusParameterBinding &_parameterBinding, Virus::Controller& _controller) :
    m_dist(_parameterBinding), m_analogBoost(_parameterBinding), m_phaser(_parameterBinding),
    m_chorus(_parameterBinding), m_eq(_parameterBinding), m_envFollow(_parameterBinding), m_punch(_parameterBinding),
    m_vocoder(_parameterBinding), m_fxMode("FX Mode"), m_parameterBinding(_parameterBinding)
{
    setupBackground(*this, m_background, BinaryData::bg_fx_1018x620_png, BinaryData::bg_fx_1018x620_pngSize);
    setupRotary(*this, m_fxSend);
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

    m_delay = std::make_unique<Delay>(_parameterBinding);
    m_reverb = std::make_unique<Reverb>(_parameterBinding);
    m_delay->setBounds(m_phaser.getRight() + 2, m_dist.getY(), 481, m_phaser.getHeight() * 2);
    m_reverb->setBounds(m_phaser.getRight() + 2, m_dist.getY(), 481, m_phaser.getHeight() * 2);

    addChildComponent(m_delay.get());
    m_delay->setVisible(true);

    addChildComponent(m_reverb.get());

    m_vocoder.setBounds(m_delay->getBounds().withY(m_delay->getBottom() + 2).withHeight(304));
    addAndMakeVisible(m_vocoder);

    m_fxMode.setBounds(m_reverb->getX()+18, m_reverb->getY()+42, comboBoxWidth, comboBoxHeight);
    addAndMakeVisible(m_fxMode);
    m_fxMode.setAlwaysOnTop(true);
    //m_fxSend.getProperties().set(Virus::LookAndFeel::KnobStyleProp, Virus::LookAndFeel::KnobStyle::GENERIC_RED);
    m_fxSend.setBounds(m_reverb->getX()+376, m_phaser.getY() - 2, knobSize, knobSize);
    addAndMakeVisible(m_fxSend);
    m_fxSend.setAlwaysOnTop(true);
    _parameterBinding.bind(m_fxSend, Virus::Param_EffectSend);
    _parameterBinding.bind(m_fxMode, Virus::Param_DelayReverbMode, 0);
    auto p = _controller.getParameter(Virus::Param_DelayReverbMode, 0);
    if (p) {
        const auto val = (int)p->getValueObject().getValueSource().getValue();
        if (val > 1 && val < 5) {
            m_fxMode.setSelectedId(val + 1, juce::dontSendNotification);
            const bool isReverb = (val > 1 && val < 5);
            m_reverb->setVisible(isReverb);
            m_delay->setVisible(!isReverb);
        }
        p->onValueChanged = nullptr;
        p->onValueChanged = [this, p]() {
            rebind();
            const auto value = (int)p->getValueObject().getValueSource().getValue();
            m_fxMode.setSelectedId(value + 1, juce::dontSendNotification);
            const bool isReverb = (value > 1 && value < 5);
            m_reverb->setVisible(isReverb);
            m_delay->setVisible(!isReverb);

        };
    }
}
void FxEditor::rebind() {
    removeChildComponent(m_delay.get());
    m_delay	= std::make_unique<FxEditor::Delay>(m_parameterBinding);
    addChildComponent(m_delay.get());
    removeChildComponent(m_reverb.get());
    m_reverb = std::make_unique<FxEditor::Reverb>(m_parameterBinding);
    addChildComponent(m_reverb.get());
    m_delay->setBounds(m_phaser.getRight() + 2, m_dist.getY(), 481, m_phaser.getHeight() * 2);
    m_reverb->setBounds(m_phaser.getRight() + 2, m_dist.getY(), 481, m_phaser.getHeight() * 2);
}
FxEditor::Distortion::Distortion(VirusParameterBinding &_parameterBinding)
{
    setupRotary(*this, m_intensity);
    m_intensity.setBounds(101, 18, knobSize, knobSize);
    addAndMakeVisible(m_curve);
    m_curve.setBounds(17, 42, comboBoxWidth, comboBoxHeight);

    _parameterBinding.bind(m_intensity, Virus::Param_DistortionIntensity);
    _parameterBinding.bind(m_curve, Virus::Param_DistortionCurve);
}

FxEditor::AnalogBoost::AnalogBoost(VirusParameterBinding &_parameterBinding)
{
    for (auto *s : {&m_boost, &m_tune})
        setupRotary(*this, *s);
    m_boost.setBounds(16, 18, knobSize, knobSize);
    m_tune.setBounds(m_boost.getBounds().withX(m_boost.getRight() - 4));

    _parameterBinding.bind(m_boost, Virus::Param_BassIntensity);
    _parameterBinding.bind(m_tune, Virus::Param_BassTune);
}

FxEditor::Phaser::Phaser(VirusParameterBinding &_parameterBinding)
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

    _parameterBinding.bind(m_rate, Virus::Param_PhaserRate);
    _parameterBinding.bind(m_freq, Virus::Param_PhaserFreq);
    _parameterBinding.bind(m_depth, Virus::Param_PhaserDepth);
    _parameterBinding.bind(m_feedback, Virus::Param_PhaserFeedback);
    _parameterBinding.bind(m_spread, Virus::Param_PhaserSpread);
    _parameterBinding.bind(m_mix, Virus::Param_PhaserMix);
    _parameterBinding.bind(m_stages, Virus::Param_PhaserMode);
}

FxEditor::Chorus::Chorus(VirusParameterBinding &_parameterBinding)
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

    _parameterBinding.bind(m_rate, Virus::Param_ChorusRate);
    _parameterBinding.bind(m_depth, Virus::Param_ChorusDepth);
    _parameterBinding.bind(m_feedback, Virus::Param_ChorusFeedback);
    _parameterBinding.bind(m_delay, Virus::Param_ChorusDelay);
    _parameterBinding.bind(m_mix, Virus::Param_ChorusMix);
    _parameterBinding.bind(m_lfoShape, Virus::Param_ChorusLfoShape);
}

FxEditor::Equalizer::Equalizer(VirusParameterBinding &_parameterBinding)
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

    _parameterBinding.bind(m_low_gain, Virus::Param_LowEqGain);
    _parameterBinding.bind(m_low_freq, Virus::Param_LowEqFreq);
    _parameterBinding.bind(m_mid_gain, Virus::Param_MidEqGain);
    _parameterBinding.bind(m_mid_freq, Virus::Param_MidEqFreq);
    _parameterBinding.bind(m_mid_q, Virus::Param_MidEqQFactor);
    _parameterBinding.bind(m_high_gain, Virus::Param_HighEqGain);
    _parameterBinding.bind(m_high_freq, Virus::Param_HighEqFreq);
}

FxEditor::EnvelopeFollower::EnvelopeFollower(VirusParameterBinding &_parameterBinding)
{
    constexpr auto y = 12;
    for (auto *s : {&m_gain, &m_attack, &m_release})
        setupRotary(*this, *s);
    m_gain.setBounds(101, y, knobSize, knobSize);
    m_attack.setBounds(m_gain.getRight() - 8, y, knobSize, knobSize);
    m_release.setBounds(m_attack.getRight() - 7, y, knobSize, knobSize);
    addAndMakeVisible(m_input);
    m_input.setBounds(17, 37, comboBoxWidth, comboBoxHeight);

    _parameterBinding.bind(m_input, Virus::Param_InputFollowMode);
    _parameterBinding.bind(m_attack, Virus::Param_FilterEnvAttack);
    _parameterBinding.bind(m_release, Virus::Param_FilterEnvDecay);
    _parameterBinding.bind(m_gain, Virus::Param_FilterEnvSustain);
}

FxEditor::Punch::Punch(VirusParameterBinding &_parameterBinding)
{
    setupRotary(*this, m_amount);
    m_amount.setBounds(19, 12, knobSize, knobSize);

    _parameterBinding.bind(m_amount, Virus::Param_PunchIntensity);
}

FxEditor::Delay::Delay(VirusParameterBinding &_parameterBinding)
{
    setupBackground(*this, m_background, BinaryData::bg_fxdelay_481x234_png, BinaryData::bg_fxdelay_481x234_pngSize);
    constexpr auto y = 18;
    for (auto *s : {&m_time, &m_rate, &m_depth, &m_color, &m_feedback})
        setupRotary(*this, *s);
    m_time.setBounds(118, 18, knobSize, knobSize);
    m_rate.setBounds(m_time.getRight() - 8, y, knobSize, knobSize);
    m_depth.setBounds(m_rate.getRight() - 8, y, knobSize, knobSize);
    m_color.setBounds(m_depth.getRight() - 3, y, knobSize, knobSize);
    m_feedback.setBounds(m_color.getRight() - 3, y, knobSize, knobSize);


    addAndMakeVisible(m_clock);
    m_clock.setBounds(18, 116+2+22, comboBoxWidth, comboBoxHeight);
    addAndMakeVisible(m_lfoShape);
    m_lfoShape.setBounds(m_clock.getBounds().getRight() + 26, 116+2+22, comboBoxWidth, comboBoxHeight);

    _parameterBinding.bind(m_clock, Virus::Param_DelayClock, 0);
    _parameterBinding.bind(m_lfoShape, Virus::Param_DelayLfoShape, 0);

    _parameterBinding.bind(m_time, Virus::Param_DelayTime, 0);
    _parameterBinding.bind(m_rate, Virus::Param_DelayRateReverbDecayTime, 0);
    _parameterBinding.bind(m_depth, Virus::Param_DelayDepthReverbRoomSize, 0);
    _parameterBinding.bind(m_color, Virus::Param_DelayColor, 0);
    _parameterBinding.bind(m_feedback, Virus::Param_DelayFeedback, 0);
}

FxEditor::Reverb::Reverb(VirusParameterBinding &_parameterBinding)
{
    setupBackground(*this, m_background, BinaryData::bg_fxreverb_481x234_png, BinaryData::bg_fxreverb_481x234_pngSize);
    constexpr auto y = 18;
    for (auto *s : {&m_time, &m_rate, &m_damping, &m_color, &m_feedback})
        setupRotary(*this, *s);
    m_time.setBounds(118, 18, knobSize, knobSize);
    m_rate.setBounds(m_time.getRight() - 8, y, knobSize, knobSize);
    m_damping.setBounds(m_rate.getRight() - 8, y, knobSize, knobSize);
    m_color.setBounds(m_damping.getRight() - 3, y, knobSize, knobSize);
    m_feedback.setBounds(m_color.getRight() - 3, y, knobSize, knobSize);
    m_reverbMode.setBounds(18, 116+2+22, comboBoxWidth, comboBoxHeight);
    addAndMakeVisible(m_reverbMode);

    _parameterBinding.bind(m_reverbMode, Virus::Param_DelayDepthReverbRoomSize, 0);
    _parameterBinding.bind(m_time, Virus::Param_DelayTime, 0);
    _parameterBinding.bind(m_rate, Virus::Param_DelayRateReverbDecayTime, 0);
    _parameterBinding.bind(m_damping, Virus::Param_DelayLfoShape, 0);
    _parameterBinding.bind(m_color, Virus::Param_DelayColor, 0);
    _parameterBinding.bind(m_feedback, Virus::Param_DelayFeedback, 0);
}

FxEditor::Vocoder::Vocoder(VirusParameterBinding &_parameterBinding) :
    m_link(true), m_carrier(_parameterBinding), m_modulator(_parameterBinding)
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

    _parameterBinding.bind(m_mode, Virus::Param_VocoderMode);
    _parameterBinding.bind(m_bands, Virus::Param_FilterEnvRelease);
    _parameterBinding.bind(m_sourceBalance, Virus::Param_FilterBalance);
    _parameterBinding.bind(m_spectralBalance, Virus::Param_FilterEnvSustainTime);
    _parameterBinding.bind(m_attack, Virus::Param_FilterEnvAttack);
    _parameterBinding.bind(m_release, Virus::Param_FilterEnvDecay);
    _parameterBinding.bind(m_link, Virus::Param_Filter2CutoffLink);
}

FxEditor::Vocoder::Carrier::Carrier(VirusParameterBinding &_parameterBinding)
{
    constexpr auto y = -4;
    for (auto *s : {&m_center_freq, &m_q_factor, &m_spread})
        setupRotary(*this, *s);
    m_center_freq.setBounds(12, -4, knobSize, knobSize);
    m_q_factor.setBounds(m_center_freq.getRight() - 8, y, knobSize, knobSize);
    m_spread.setBounds(m_q_factor.getRight() - 7, y, knobSize, knobSize);

    _parameterBinding.bind(m_center_freq, Virus::Param_FilterCutA);
    _parameterBinding.bind(m_q_factor, Virus::Param_FilterResA);
    _parameterBinding.bind(m_spread, Virus::Param_FilterKeyFollowA);
}

FxEditor::Vocoder::Modulator::Modulator(VirusParameterBinding &_parameterBinding)
{
    constexpr auto y = -4;
    for (auto *s : {&m_freq_offset, &m_q_factor, &m_spread})
        setupRotary(*this, *s);
    m_freq_offset.setBounds(107, y, knobSize, knobSize);
    m_q_factor.setBounds(m_freq_offset.getRight() - 8, y, knobSize, knobSize);
    m_spread.setBounds(m_q_factor.getRight() - 7, y, knobSize, knobSize);
    addAndMakeVisible(m_modInput);
    m_modInput.setBounds(8, 23, comboBoxWidth, comboBoxHeight);

    _parameterBinding.bind(m_freq_offset, Virus::Param_FilterCutB);
    _parameterBinding.bind(m_q_factor, Virus::Param_FilterResB);
    _parameterBinding.bind(m_spread, Virus::Param_FilterKeyFollowB);
    _parameterBinding.bind(m_modInput, Virus::Param_InputSelect);
}

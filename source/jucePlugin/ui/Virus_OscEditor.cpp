#include "Virus_OscEditor.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "../VirusParameterBinding.h"

using namespace juce;

constexpr auto comboBoxWidth = 84;

OscEditor::OscEditor(VirusParameterBinding& _parameterBinding)
: m_oscOne(_parameterBinding, 0), m_oscTwo(_parameterBinding), m_oscThree(_parameterBinding)
, m_unison(_parameterBinding), m_mixer(_parameterBinding), m_ringMod(_parameterBinding), m_sub(_parameterBinding), m_portamento(_parameterBinding), m_filters(_parameterBinding), m_filterEnv(_parameterBinding), m_ampEnv(_parameterBinding)
{
    setupBackground(*this, m_background, BinaryData::bg_osc_1018x620_png, BinaryData::bg_osc_1018x620_pngSize);
    setBounds(m_background->getDrawableBounds().toNearestIntEdges());

    addAndMakeVisible(m_oscOne);
    addAndMakeVisible(m_oscTwo);
    addAndMakeVisible(m_oscThree);
    addAndMakeVisible(m_unison);
    addAndMakeVisible(m_mixer);
    addAndMakeVisible(m_ringMod);
    addAndMakeVisible(m_sub);
    addAndMakeVisible(m_portamento);
    addAndMakeVisible(m_filters);
    addAndMakeVisible(m_filterEnv);
    addAndMakeVisible(m_ampEnv);
    addAndMakeVisible(m_oscSync);

    _parameterBinding.bind(m_oscSync, Virus::Param_Osc2Sync);
}

void OscEditor::resized()
{
    m_oscOne.setBounds(23, 28, 379, 116);
    m_oscTwo.setBounds(m_oscOne.getBounds().translated(0, 119).withHeight(216));
    m_oscThree.setBounds(m_oscTwo.getX(), m_oscTwo.getBottom(), m_oscTwo.getWidth(), 116);
    m_unison.setBounds(m_oscThree.getBounds().translated(0, 119));
    m_mixer.setBounds(m_oscOne.getBounds().translated(381, 0).withWidth(165));
    m_ringMod.setBounds(m_mixer.getBounds().translated(0, m_mixer.getHeight() + 2).withHeight(216));
    m_sub.setBounds(m_ringMod.getBounds().withY(m_ringMod.getBottom() + 2).withHeight(m_mixer.getHeight()));
    m_portamento.setBounds(m_sub.getX(), m_sub.getBottom() + 2, m_sub.getWidth(), 114);
    m_filters.setBounds(m_mixer.getRight() + 2, m_mixer.getY(), 445, 334);
    m_filterEnv.setBounds(m_filters.getX(), m_filters.getBottom() + 2, m_filters.getWidth(), m_oscOne.getHeight());
    m_ampEnv.setBounds(m_filterEnv.getBounds().withY(m_filterEnv.getBounds().getBottom() + 2));
    m_oscSync.setBounds(319, roundToInt(4 + m_oscOne.getBottom() - Buttons::SyncButton::kHeight * 0.5),
                        Buttons::SyncButton::kWidth, Buttons::SyncButton::kHeight);
}

OscEditor::OscOne::OscOne(VirusParameterBinding& _parameterBinding, uint32_t _oscIndex)
{
    for (auto *s : {&m_shape, &m_pulseWidth, &m_semitone, &m_keyFollow})
        setupRotary(*this, *s);
    m_shape.setBounds(101, 17, knobSize, knobSize);
    m_pulseWidth.setBounds(m_shape.getBounds().translated(62, 0));
    m_semitone.setBounds(m_pulseWidth.getBounds().translated(63, 0));
    m_keyFollow.setBounds(m_semitone.getBounds().translated(66, 0));

    addAndMakeVisible(m_waveSelect);
    m_waveSelect.setBounds (18, 42, comboBoxWidth, comboBoxHeight);

    _parameterBinding.bind(m_semitone, _oscIndex == 0 ? Virus::Param_Osc1Semitone : Virus::Param_Osc2Semitone);
    _parameterBinding.bind(m_shape, _oscIndex == 0 ? Virus::Param_Osc1Shape : Virus::Param_Osc2Shape);
    _parameterBinding.bind(m_pulseWidth, _oscIndex == 0 ? Virus::Param_Osc1PW : Virus::Param_Osc2PW);
    _parameterBinding.bind(m_keyFollow, _oscIndex == 0 ? Virus::Param_Osc1Keyfollow : Virus::Param_Osc2Keyfollow);
    _parameterBinding.bind(m_waveSelect, _oscIndex == 0 ? Virus::Param_Osc1Wave : Virus::Param_Osc2Wave);
}

OscEditor::OscTwo::OscTwo(VirusParameterBinding& _parameterBinding) : OscOne(_parameterBinding, 1)
{
    for (auto *s : {&m_fmAmount, &m_detune, &m_envFm, &m_envOsc2})
        setupRotary(*this, *s);
    m_fmAmount.setBounds(m_shape.getBounds().translated(0, 95));
    m_detune.setBounds(m_fmAmount.getBounds().translated(62, 0));
    m_envFm.setBounds(m_detune.getBounds().translated(63, 0));
    m_envOsc2.setBounds(m_envFm.getBounds().translated(66, 0));
    addAndMakeVisible (m_fmMode);
    m_fmMode.setBounds (18, 140, comboBoxWidth, comboBoxHeight);

    _parameterBinding.bind(m_fmAmount, Virus::Param_Osc2FMAmount);
    _parameterBinding.bind(m_detune, Virus::Param_Osc2Detune);
    _parameterBinding.bind(m_envFm, Virus::Param_FMFiltEnvAmt);
    _parameterBinding.bind(m_envOsc2, Virus::Param_Osc2FltEnvAmt);
    _parameterBinding.bind(m_fmMode, Virus::Param_OscFMMode);
}

OscEditor::OscThree::OscThree(VirusParameterBinding& _parameterBinding)
{
    for (auto *s : {&m_semitone, &m_detune, &m_level})
        setupRotary(*this, *s);
    m_semitone.setBounds(101, 17, knobSize, knobSize);
    m_detune.setBounds(m_semitone.getBounds().translated(62, 0));
    m_level.getProperties().set(Virus::LookAndFeel::KnobStyleProp, Virus::LookAndFeel::KnobStyle::GENERIC_RED);
    m_level.setBounds(m_detune.getBounds().translated(63, 0));
    addAndMakeVisible (m_oscThreeMode);
    m_oscThreeMode.setBounds (18, 43, comboBoxWidth, comboBoxHeight);

    _parameterBinding.bind(m_semitone, Virus::Param_Osc3Semitone);
    _parameterBinding.bind(m_detune, Virus::Param_Osc3Detune);
    _parameterBinding.bind(m_level, Virus::Param_Osc3Volume);
    _parameterBinding.bind(m_oscThreeMode, Virus::Param_Osc3Mode);
}

OscEditor::Unison::Unison(VirusParameterBinding& _parameterBinding)
{
    for (auto *s : {&m_detune, &m_panSpread, &m_lfoPhase, &m_phaseInit})
        setupRotary(*this, *s);
    m_detune.setBounds(101, 17, knobSize, knobSize);
    m_panSpread.setBounds(m_detune.getBounds().translated(62, 0));
    m_lfoPhase.setBounds(m_panSpread.getBounds().translated(63, 0));
    m_phaseInit.setBounds(m_lfoPhase.getBounds().translated(66, 0));
    addAndMakeVisible (m_unisonVoices);
    m_unisonVoices.setBounds (18, 42, comboBoxWidth, comboBoxHeight);

    _parameterBinding.bind(m_detune, Virus::Param_UnisonDetune);
    _parameterBinding.bind(m_panSpread, Virus::Param_UnisonPanSpread);
    _parameterBinding.bind(m_lfoPhase, Virus::Param_UnisonLfoPhase);
    _parameterBinding.bind(m_phaseInit, Virus::Param_OscInitPhase);
    _parameterBinding.bind(m_unisonVoices, Virus::Param_UnisonMode);
}

OscEditor::Mixer::Mixer(VirusParameterBinding& _parameterBinding)
{
    for (auto *s : {&m_oscBalance, &m_oscLevel})
        setupRotary(*this, *s);
    m_oscBalance.setBounds(14, 18, knobSize, knobSize);
    m_oscLevel.getProperties().set(Virus::LookAndFeel::KnobStyleProp, Virus::LookAndFeel::KnobStyle::GENERIC_RED);
    m_oscLevel.setBounds(m_oscBalance.getBounds().translated(knobSize, 0));

    _parameterBinding.bind(m_oscBalance, Virus::Param_OscBalance);
    _parameterBinding.bind(m_oscLevel, Virus::Param_OscMainVolume);
}

OscEditor::RingMod::RingMod(VirusParameterBinding& _parameterBinding)
{
    for (auto *s : {&m_noiseLevel, &m_ringModLevel, &m_noiseColor})
        setupRotary(*this, *s);
    m_noiseLevel.getProperties().set(Virus::LookAndFeel::KnobStyleProp, Virus::LookAndFeel::KnobStyle::GENERIC_RED);
    m_noiseLevel.setBounds(14, 20, knobSize, knobSize);
    m_ringModLevel.getProperties().set(Virus::LookAndFeel::KnobStyleProp, Virus::LookAndFeel::KnobStyle::GENERIC_RED);
    m_ringModLevel.setBounds(m_noiseLevel.getBounds().translated(knobSize, 0));
    m_noiseColor.setBounds(m_noiseLevel.getBounds().translated(0, 95));

    _parameterBinding.bind(m_noiseLevel, Virus::Param_NoiseVolume);
    _parameterBinding.bind(m_ringModLevel, Virus::Param_RingModMVolume);
    _parameterBinding.bind(m_noiseColor, Virus::Param_NoiseColor);
}

OscEditor::Sub::Sub(VirusParameterBinding& _parameterBinding)
{
    setupRotary(*this, m_level);
    m_level.getProperties().set(Virus::LookAndFeel::KnobStyleProp, Virus::LookAndFeel::KnobStyle::GENERIC_RED);
    m_level.setBounds(14 + knobSize, 20, knobSize, knobSize);
    m_subWaveform.setBounds(30, 34, Buttons::HandleButton::kWidth, Buttons::HandleButton::kHeight);
    addAndMakeVisible(m_subWaveform);

    _parameterBinding.bind(m_level, Virus::Param_SubOscVolume);
    _parameterBinding.bind(m_subWaveform, Virus::Param_SubOscShape);
}

OscEditor::Portamento::Portamento(VirusParameterBinding& _parameterBinding)
{
    setupRotary(*this, m_portamento);
    m_portamento.setBounds(12, 18, knobSize, knobSize);

    _parameterBinding.bind(m_portamento, Virus::Param_PortamentoTime);
}

OscEditor::Filters::Filters(VirusParameterBinding &_parameterBinding) :	m_filter{Filter(_parameterBinding,0),Filter(_parameterBinding,1)}, m_link1(true), m_link2(true)
{
    addAndMakeVisible(m_filter[0]);
    addAndMakeVisible(m_filter[1]);
    m_filter[0].setBounds(28, 17, 390, knobSize);
    m_filter[1].setBounds(m_filter[0].getBounds().translated(0, 215));
    setupRotary(*this, m_filterBalance);
    m_filterBalance.setBounds(132, 137, knobSize, knobSize);
    addAndMakeVisible(m_filterBalance);

    m_envPol[0].setBounds(317, 179, 27, 33);
    m_envPol[1].setBounds(m_envPol[0].getBounds().translated(48, 0));
    addAndMakeVisible(m_envPol[0]);
    addAndMakeVisible(m_envPol[1]);

    addAndMakeVisible(m_link1);
    m_link1.setBounds(7, 143, 12, 36);
    addAndMakeVisible(m_link2);
    m_link2.setBounds(m_link1.getBounds().withX(426));

    for (auto* combo : {&m_filterMode[0], &m_filterMode[1], &m_filterRouting, &m_saturationCurve, &m_keyFollowBase})
        addAndMakeVisible (combo);
    m_filterMode[0].setBounds (32, 128, comboBoxWidth, comboBoxHeight);
    m_filterMode[1].setBounds (32, 178, comboBoxWidth, comboBoxHeight);
    m_filterRouting.setBounds (m_filterMode[0].getBounds().translated (184, 0));
    m_saturationCurve.setBounds (m_filterMode[1].getBounds().translated (184, 0));
    m_keyFollowBase.setBounds (m_filterMode[0].getBounds().translated (286, 0));

    _parameterBinding.bind(m_filterBalance, Virus::Param_FilterBalance);
    _parameterBinding.bind(m_filterMode[0], Virus::Param_FilterModeA);
    _parameterBinding.bind(m_filterMode[1], Virus::Param_FilterModeB);
    _parameterBinding.bind(m_filterRouting, Virus::Param_FilterRouting);
    _parameterBinding.bind(m_saturationCurve, Virus::Param_SaturationCurve);
    _parameterBinding.bind(m_keyFollowBase, Virus::Param_FilterKeyFollowA); // maybe not right
    _parameterBinding.bind(m_link1, Virus::Param_Filter2CutoffLink);
    _parameterBinding.bind(m_envPol[0].m_pos, Virus::Param_Filter1EnvPolarity);
    _parameterBinding.bind(m_envPol[1].m_pos, Virus::Param_Filter2EnvPolarity);
}

OscEditor::Filters::Filter::Filter(VirusParameterBinding& _parameterBinding, const uint8_t _fltIndex)
{
    for (auto *s : {&m_cutoff, &m_res, &m_envAmount, &m_keyTrack, &m_resVel, &m_envVel})
        setupRotary(*this, *s);
    m_cutoff.setBounds(0, 0, knobSize, knobSize);
    m_res.setBounds(m_cutoff.getBounds().translated(knobSize - 9, 0));
    m_envAmount.setBounds(m_res.getBounds().translated(knobSize - 6, 0));
    m_keyTrack.setBounds(m_envAmount.getBounds().translated(knobSize - 5, 0));
    m_resVel.setBounds(m_keyTrack.getBounds().translated(knobSize - 5, 0));
    m_envVel.setBounds(m_resVel.getBounds().translated(knobSize - 5, 0));

    _parameterBinding.bind(m_cutoff, _fltIndex == 0 ? Virus::Param_FilterCutA : Virus::Param_FilterCutB);
    _parameterBinding.bind(m_res, _fltIndex == 0 ? Virus::Param_FilterResA: Virus::Param_FilterResB);
    _parameterBinding.bind(m_envAmount, _fltIndex == 0 ? Virus::Param_FilterEnvAmtA : Virus::Param_FilterEnvAmtB);
    _parameterBinding.bind(m_keyTrack, _fltIndex == 0 ? Virus::Param_FilterKeyFollowA : Virus::Param_FilterKeyFollowB);
    _parameterBinding.bind(m_resVel, _fltIndex == 0 ? Virus::Param_Resonance1Velocity : Virus::Param_Resonance2Velocity);
    _parameterBinding.bind(m_envVel, _fltIndex == 0 ? Virus::Param_Filter1EnvAmtVelocity : Virus::Param_Filter2EnvAmtVelocity);
}

OscEditor::Envelope::Envelope(VirusParameterBinding& _parameterBinding, Virus::EnvelopeType _envIndex)
{
    for (auto *s : {&m_attack, &m_decay, &m_sustain, &m_time, &m_release})
        setupRotary(*this, *s);
    m_attack.setBounds(28, 17, knobSize, knobSize);
    m_decay.setBounds(m_attack.getBounds().translated(knobSize + 10, 0));
    m_sustain.setBounds(m_decay.getBounds().translated(knobSize + 9, 0));
    m_time.setBounds(m_sustain.getBounds().translated(knobSize + 9, 0));
    m_release.setBounds(m_time.getBounds().translated(knobSize + 12, 0));

    _parameterBinding.bind(m_attack,
                           _envIndex == Virus::Env_Amp ? Virus::Param_AmpEnvAttack : Virus::Param_FilterEnvAttack);
    _parameterBinding.bind(m_decay,
                           _envIndex == Virus::Env_Amp ? Virus::Param_AmpEnvDecay : Virus::Param_FilterEnvDecay);
    _parameterBinding.bind(m_sustain,
                           _envIndex == Virus::Env_Amp ? Virus::Param_AmpEnvSustain : Virus::Param_FilterEnvSustain);
    _parameterBinding.bind(m_release,
                           _envIndex == Virus::Env_Amp ? Virus::Param_AmpEnvRelease : Virus::Param_FilterEnvRelease);
    _parameterBinding.bind(m_time,
                           _envIndex == Virus::Env_Amp ? Virus::Param_AmpEnvSustainTime : Virus::Param_FilterEnvSustainTime);
}

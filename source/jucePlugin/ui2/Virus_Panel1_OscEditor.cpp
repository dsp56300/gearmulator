#include "Virus_Panel1_OscEditor.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "../VirusParameterBinding.h"

using namespace juce;

OscEditor::OscEditor(VirusParameterBinding& _parameterBinding)
{
	setupBackground(*this, m_background, BinaryData::panel_1_png, BinaryData::panel_1_pngSize);
    setBounds(m_background->getDrawableBounds().toNearestIntEdges());

	//setLookAndFeel(&m_lookAndFeel);

    for (auto *s : {&m_osc1Shape, &m_osc1PulseWidth, &m_osc1Semitone, &m_osc1KeyFollow})
	{
		setupRotary(*this, *s);
	}
	
    //OSC 1
	m_osc1Shape.setBounds(287 - knobSize / 2, 103 - knobSize / 2, knobSize, knobSize);
	m_osc1PulseWidth.setBounds(433 - knobSize / 2, 103 - knobSize / 2, knobSize, knobSize);
	m_osc1Semitone.setBounds(577 - knobSize / 2, 103 - knobSize / 2, knobSize, knobSize);
	m_osc1KeyFollow.setBounds(722 - knobSize / 2, 103 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_osc1WaveSelect);
	m_osc1WaveSelect.setBounds(114+comboBoxXMargin - comboBoxWidth / 2, 103 - comboBoxHeight / 2, comboBoxWidth, comboBoxHeight);

	_parameterBinding.bind(m_osc1Semitone, Virus::Param_Osc1Semitone);
	_parameterBinding.bind(m_osc1Shape, Virus::Param_Osc1Shape);
	_parameterBinding.bind(m_osc1PulseWidth, Virus::Param_Osc1PW );
	_parameterBinding.bind(m_osc1KeyFollow, Virus::Param_Osc1Keyfollow );
	_parameterBinding.bind(m_osc1WaveSelect, Virus::Param_Osc1Wave);

    //OSC 2
	for (auto *s : {&m_osc2Shape, &m_osc2PulseWidth, &m_osc2Semitone, &m_osc2KeyFollow})
	{
		setupRotary(*this, *s);
	}

	m_osc2Shape.setBounds(287 - knobSize / 2, 311 - knobSize / 2, knobSize, knobSize);
	m_osc2PulseWidth.setBounds(433 - knobSize / 2, 311 - knobSize / 2, knobSize, knobSize);
	m_osc2Semitone.setBounds(577 - knobSize / 2, 311 - knobSize / 2, knobSize, knobSize);
	m_osc2KeyFollow.setBounds(722 - knobSize / 2, 311 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_osc2WaveSelect);
	m_osc2WaveSelect.setBounds(114+comboBoxXMargin - comboBoxWidth / 2, 311 - comboBoxHeight / 2, comboBoxWidth, comboBoxHeight);

	for (auto *s : {&m_osc2FmAmount, &m_osc2Detune, &m_osc2EnvFm, &m_osc2envOSC, &m_osc2PhaseInit})
	{
		setupRotary(*this, *s);
	}

	m_osc2FmAmount.setBounds(287 - knobSize / 2, 485 - knobSize / 2, knobSize, knobSize);
	m_osc2Detune.setBounds(433 - knobSize / 2, 485 - knobSize / 2, knobSize, knobSize);
	m_osc2EnvFm.setBounds(577 - knobSize / 2, 485 - knobSize / 2, knobSize, knobSize);
	m_osc2envOSC.setBounds(722 - knobSize / 2, 485 - knobSize / 2, knobSize, knobSize);
	m_osc2PhaseInit.setBounds(871 - knobSize / 2, 485 - knobSize / 2, knobSize, knobSize);

    addAndMakeVisible(m_osc2FmMode);
	m_osc2FmMode.setBounds(114+comboBoxXMargin - comboBoxWidth / 2, 485 - comboBoxHeight / 2, comboBoxWidth, comboBoxHeight);
	
    addAndMakeVisible(m_syncOsc1Osc2);
	m_syncOsc1Osc2.setBounds(871 - m_syncOsc1Osc2.kWidth/ 2, 308 - m_syncOsc1Osc2.kHeight / 2, m_syncOsc1Osc2.kWidth, m_syncOsc1Osc2.kHeight);  

    _parameterBinding.bind(m_osc2Semitone, Virus::Param_Osc2Semitone);
	_parameterBinding.bind(m_osc2Shape, Virus::Param_Osc2Shape);
	_parameterBinding.bind(m_osc2PulseWidth, Virus::Param_Osc2PW);
	_parameterBinding.bind(m_osc2KeyFollow, Virus::Param_Osc2Keyfollow);
	_parameterBinding.bind(m_osc2WaveSelect, Virus::Param_Osc2Wave);

	_parameterBinding.bind(m_osc2FmAmount, Virus::Param_Osc2FMAmount);
	_parameterBinding.bind(m_osc2Detune, Virus::Param_Osc2Detune);
	_parameterBinding.bind(m_osc2EnvFm, Virus::Param_FMFiltEnvAmt);
	_parameterBinding.bind(m_osc2envOSC, Virus::Param_Osc2FltEnvAmt);
	_parameterBinding.bind(m_osc2PhaseInit, Virus::Param_OscInitPhase);
	_parameterBinding.bind(m_osc2FmMode, Virus::Param_OscFMMode);
	_parameterBinding.bind(m_syncOsc1Osc2, Virus::Param_Osc2Sync);
    
    //OSC 3
    for (auto *s : {&m_osc3Semitone, &m_osc3Detune, &m_osc3Level})
	{
		setupRotary(*this, *s);
	}
	
	m_osc3Semitone.setBounds(287 - knobSize / 2, 694 - knobSize / 2, knobSize, knobSize);
	m_osc3Detune.setBounds(434 - knobSize / 2, 694 - knobSize / 2, knobSize, knobSize);
	m_osc3Mode.setBounds(114+comboBoxXMargin - comboBoxWidth / 2, 694 - comboBoxHeight / 2, comboBoxWidth, comboBoxHeight);
	addAndMakeVisible(m_osc3Mode);

	_parameterBinding.bind(m_osc3Semitone, Virus::Param_Osc3Semitone);
	_parameterBinding.bind(m_osc3Detune, Virus::Param_Osc3Detune);
	_parameterBinding.bind(m_osc3Mode, Virus::Param_Osc3Mode);

    // OSC SUB
	setupRotary(*this, m_noiseColor);
	m_noiseColor.setBounds(841 - knobSize / 2, 695 - knobSize / 2, knobSize, knobSize);
	m_subWaveform.setBounds(656 - m_subWaveform.kWidth / 2, 692 - m_subWaveform.kHeight / 2, m_subWaveform.kWidth, m_subWaveform.kHeight);
	addAndMakeVisible(m_subWaveform);
	_parameterBinding.bind(m_noiseColor, Virus::Param_NoiseColor);
	_parameterBinding.bind(m_subWaveform, Virus::Param_SubOscShape);

    //UNISON
    for (auto *s : {&m_detune, &m_panSpread, &m_lfoPhase})
	{
		setupRotary(*this, *s);
	}

	m_detune.setBounds(287 - knobSize / 2, 903 - knobSize / 2, knobSize, knobSize);
	m_panSpread.setBounds(434 - knobSize / 2, 903 - knobSize / 2, knobSize, knobSize);
	m_lfoPhase.setBounds(577 - knobSize / 2, 903 - knobSize / 2, knobSize, knobSize);
	addAndMakeVisible(m_unisonVoices);
	m_unisonVoices.setBounds(114+comboBoxXMargin - comboBoxWidth / 2, 903 - comboBoxHeight / 2, comboBoxWidth, comboBoxHeight);

	_parameterBinding.bind(m_detune, Virus::Param_UnisonDetune);
	_parameterBinding.bind(m_panSpread, Virus::Param_UnisonPanSpread);
	_parameterBinding.bind(m_lfoPhase, Virus::Param_UnisonLfoPhase);
	_parameterBinding.bind(m_unisonVoices, Virus::Param_UnisonMode);

    // Punch
	setupRotary(*this, m_Punch);
	m_Punch.setBounds(841 - knobSize / 2, 903 - knobSize / 2, knobSize, knobSize);
	_parameterBinding.bind(m_Punch, Virus::Param_PunchIntensity);

    // Mixer
    for (auto *s : {&m_oscBalance, &m_oscLevel, &m_osc3Level, &m_oscSublevel, &m_noiseLevel, &m_ringModLevel})
	{
		setupRotary(*this, *s);
	}

    m_oscBalance.setBounds(1260 - knobSize / 2, 102 - knobSize / 2, knobSize, knobSize);
	m_oscLevel.setBounds(1260 - knobSize / 2, 263 - knobSize / 2, knobSize, knobSize);
	m_osc3Level.setBounds(1260 - knobSize / 2, 422 - knobSize / 2, knobSize, knobSize);
	m_oscSublevel.setBounds(1260 - knobSize / 2, 579 - knobSize / 2, knobSize, knobSize);
	m_noiseLevel.setBounds(1260 - knobSize / 2, 744 - knobSize / 2, knobSize, knobSize);
	m_ringModLevel.setBounds(1260 - knobSize / 2, 903 - knobSize / 2, knobSize, knobSize);

	_parameterBinding.bind(m_oscBalance, Virus::Param_OscBalance);
	_parameterBinding.bind(m_oscLevel, Virus::Param_OscMainVolume);
	_parameterBinding.bind(m_osc3Level, Virus::Param_Osc3Volume);
    _parameterBinding.bind(m_oscSublevel, Virus::Param_SubOscVolume);
	_parameterBinding.bind(m_noiseLevel, Virus::Param_NoiseVolume);
	_parameterBinding.bind(m_ringModLevel, Virus::Param_RingModMVolume);

    //Velo mod
	for (auto *s : {&m_osc1ShapeVelocity, &m_pulseWidthVelocity, &m_ampVelocity, &m_panoramaVelocity, &m_osc2ShapeVelocity, &m_fmAmountVelocity})
	{
		setupRotary(*this, *s);
	}

	m_osc1ShapeVelocity.setBounds(1067 - knobSize / 2, 102 - knobSize / 2, knobSize, knobSize);
	m_osc2ShapeVelocity.setBounds(1067 - knobSize / 2, 264 - knobSize / 2, knobSize, knobSize);
	m_pulseWidthVelocity.setBounds(1067 - knobSize / 2, 421 - knobSize / 2, knobSize, knobSize);
	m_fmAmountVelocity.setBounds(1067 - knobSize / 2, 579 - knobSize / 2, knobSize, knobSize);
	m_ampVelocity.setBounds(1067 - knobSize / 2, 743 - knobSize / 2, knobSize, knobSize);
	m_panoramaVelocity.setBounds(1067 - knobSize / 2, 903 - knobSize / 2, knobSize, knobSize);

	_parameterBinding.bind(m_osc1ShapeVelocity, Virus::Param_Osc1ShapeVelocity);
	_parameterBinding.bind(m_pulseWidthVelocity, Virus::Param_PulseWidthVelocity);
	_parameterBinding.bind(m_ampVelocity, Virus::Param_AmpVelocity);
	_parameterBinding.bind(m_panoramaVelocity, Virus::Param_PanoramaVelocity);
	_parameterBinding.bind(m_osc2ShapeVelocity, Virus::Param_Osc2ShapeVelocity);
	_parameterBinding.bind(m_fmAmountVelocity, Virus::Param_FmAmountVelocity);

	//Filters
    for (auto *s : {&m_cutoff1, &m_res1, &m_envAmount1, &m_keyTrack1, &m_resVel1, &m_envVel1, &m_cutoff2, &m_res2, &m_envAmount2, &m_keyTrack2, &m_resVel2, &m_envVel2, &m_filterBalance})
	{
		setupRotary(*this, *s);
	}

	m_cutoff1.setBounds(1474 - knobSize / 2, 102 - knobSize / 2, knobSize, knobSize);
	m_res1.setBounds(1618 - knobSize / 2, 102 - knobSize / 2, knobSize, knobSize);
	m_envAmount1.setBounds(1762 - knobSize / 2, 102 - knobSize / 2, knobSize, knobSize);
	m_keyTrack1.setBounds(1909 - knobSize / 2, 102 - knobSize / 2, knobSize, knobSize);
	m_resVel1.setBounds(2053 - knobSize / 2, 102 - knobSize / 2, knobSize, knobSize);
	m_envVel1.setBounds(2193 - knobSize / 2, 102 - knobSize / 2, knobSize, knobSize);
	m_cutoff2.setBounds(1474 - knobSize / 2, 485 - knobSize / 2, knobSize, knobSize);
	m_res2.setBounds(1618 - knobSize / 2, 485 - knobSize / 2, knobSize, knobSize);
	m_envAmount2.setBounds(1762 - knobSize / 2, 485 - knobSize / 2, knobSize, knobSize);
	m_keyTrack2.setBounds(1909 - knobSize / 2, 485 - knobSize / 2, knobSize, knobSize);
	m_resVel2.setBounds(2053 - knobSize / 2, 485 - knobSize / 2, knobSize, knobSize);
	m_envVel2.setBounds(2193 - knobSize / 2, 485 - knobSize / 2, knobSize, knobSize);
	m_filterBalance.setBounds(1522 - knobSize / 2, 302 - knobSize / 2, knobSize, knobSize);

	m_filterMode1.setBounds(1710+comboBoxXMargin - comboBox3Width / 2, 239 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
	m_filterMode2.setBounds(1710+comboBoxXMargin - comboBox3Width / 2, 353 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
	m_filterRouting.setBounds(2017+comboBoxXMargin - comboBox3Width / 2, 242 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
	m_saturationCurve.setBounds(2017+comboBoxXMargin - comboBox3Width / 2, 353 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
	m_keyFollowBase.setBounds(1876+comboBoxXMargin - comboBox2Width / 2, 303 - comboBox2Height / 2, comboBox2Width, comboBox2Height);

	addAndMakeVisible(m_filterMode1);
	addAndMakeVisible(m_filterMode2);
	addAndMakeVisible(m_filterRouting);
	addAndMakeVisible(m_saturationCurve);
	addAndMakeVisible(m_keyFollowBase);

	m_envPol1.setBounds(2166 - m_envPol1.kWidth / 2, 241 - m_envPol1.kHeight / 2, m_envPol1.kWidth, m_envPol1.kHeight);  
	m_envPol2.setBounds(2166 - m_envPol2.kWidth / 2, 351 - m_envPol2.kHeight / 2, m_envPol2.kWidth, m_envPol2.kHeight);  
	m_link1.setBounds(1401 - m_link1.kWidth / 2, 302 - m_link1.kHeight / 2, m_link1.kWidth, m_link1.kHeight);
	m_link2.setBounds(2266 - m_link2.kWidth / 2, 302 - m_link2.kHeight / 2, m_link2.kWidth, m_link2.kHeight);  

	addAndMakeVisible(m_envPol1);
	addAndMakeVisible(m_envPol2);
	addAndMakeVisible(m_link1);
	addAndMakeVisible(m_link2);

	_parameterBinding.bind(m_cutoff1, Virus::Param_FilterCutA);
	_parameterBinding.bind(m_res1, Virus::Param_FilterResA);
	_parameterBinding.bind(m_envAmount1, Virus::Param_FilterEnvAmtA);
	_parameterBinding.bind(m_keyTrack1, Virus::Param_FilterKeyFollowA);
	_parameterBinding.bind(m_resVel1, Virus::Param_Resonance1Velocity);
	_parameterBinding.bind(m_envVel1, Virus::Param_Filter1EnvAmtVelocity);
	_parameterBinding.bind(m_cutoff2, Virus::Param_FilterCutB);
	_parameterBinding.bind(m_res2, Virus::Param_FilterResB);
	_parameterBinding.bind(m_envAmount2, Virus::Param_FilterEnvAmtB);
	_parameterBinding.bind(m_keyTrack2, Virus::Param_FilterKeyFollowB);
	_parameterBinding.bind(m_resVel2, Virus::Param_Resonance2Velocity);
	_parameterBinding.bind(m_envVel2, Virus::Param_Filter2EnvAmtVelocity);
	_parameterBinding.bind(m_filterBalance, Virus::Param_FilterBalance);

	_parameterBinding.bind(m_filterMode1, Virus::Param_FilterModeA);
	_parameterBinding.bind(m_filterMode2, Virus::Param_FilterModeB);
	_parameterBinding.bind(m_filterRouting, Virus::Param_FilterRouting);
	_parameterBinding.bind(m_saturationCurve, Virus::Param_SaturationCurve);
	_parameterBinding.bind(m_keyFollowBase, Virus::Param_FilterKeytrackBase);

	_parameterBinding.bind(m_envPol1, Virus::Param_Filter1EnvPolarity);
	_parameterBinding.bind(m_envPol2, Virus::Param_Filter2EnvPolarity);
	_parameterBinding.bind(m_link1, Virus::Param_Filter2CutoffLink);
	//todo no parameter for link2!
	//_parameterBinding.bind(m_link2, Virus::Param_Filter );

	//Env filter
	for (auto *s : {&m_fltAttack, &m_fltDecay, &m_fltSustain, &m_fltTime, &m_fltRelease})
	{
		setupRotary(*this, *s);
	}

	m_fltAttack.setBounds(1474 - knobSize / 2, 695 - knobSize / 2, knobSize, knobSize);
	m_fltDecay.setBounds(1648 - knobSize / 2, 695 - knobSize / 2, knobSize, knobSize);
	m_fltSustain.setBounds(1830 - knobSize / 2, 695 - knobSize / 2, knobSize, knobSize);
	m_fltTime.setBounds(2016 - knobSize / 2, 695 - knobSize / 2, knobSize, knobSize);
	m_fltRelease.setBounds(2193 - knobSize / 2, 695 - knobSize / 2, knobSize, knobSize);

	_parameterBinding.bind(m_fltAttack, Virus::Param_FilterEnvAttack);
	_parameterBinding.bind(m_fltDecay, Virus::Param_FilterEnvDecay);
	_parameterBinding.bind(m_fltSustain, Virus::Param_FilterEnvSustain);
	_parameterBinding.bind(m_fltTime, Virus::Param_FilterEnvSustainTime);
	_parameterBinding.bind(m_fltRelease, Virus::Param_FilterEnvRelease);

	//Env Amp
	for (auto *s : {&m_ampAttack, &m_ampDecay, &m_ampSustain, &m_ampTime, &m_ampRelease})
	{
		setupRotary(*this, *s);
	}

	m_ampAttack.setBounds(1474 - knobSize / 2, 904 - knobSize / 2, knobSize, knobSize);
	m_ampDecay.setBounds(1648 - knobSize / 2, 904 - knobSize / 2, knobSize, knobSize);
	m_ampSustain.setBounds(1830 - knobSize / 2, 904 - knobSize / 2, knobSize, knobSize);
	m_ampTime.setBounds(2016 - knobSize / 2, 904 - knobSize / 2, knobSize, knobSize);
	m_ampRelease.setBounds(2193 - knobSize / 2, 904 - knobSize / 2, knobSize, knobSize);

	_parameterBinding.bind(m_ampAttack, Virus::Param_AmpEnvAttack);
	_parameterBinding.bind(m_ampDecay, Virus::Param_AmpEnvDecay);
	_parameterBinding.bind(m_ampSustain, Virus::Param_AmpEnvSustain);
	_parameterBinding.bind(m_ampTime, Virus::Param_AmpEnvSustainTime);
	_parameterBinding.bind(m_ampRelease, Virus::Param_AmpEnvRelease);
}

void OscEditor::resized()
{
}
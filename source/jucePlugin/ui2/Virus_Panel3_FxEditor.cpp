#include "Virus_Panel3_FxEditor.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "../VirusParameterBinding.h"

using namespace juce;


FxEditor::FxEditor(VirusParameterBinding &_parameterBinding, AudioPluginAudioProcessor &_processorRef): m_controller(_processorRef.getController())
{
	setupBackground(*this, m_background, BinaryData::panel_3_png, BinaryData::panel_3_pngSize);
    setBounds(m_background->getDrawableBounds().toNearestIntEdges());

	// envFollow
    for (auto *s : {&m_envFollowLevel, &m_envFollowAttack, &m_eEnvFollowRelease})
	{
		setupRotary(*this, *s);
	}

	m_envFollowLevel.setBounds(219 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);
	m_envFollowAttack.setBounds(365 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);
	m_eEnvFollowRelease.setBounds(508 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);

    addAndMakeVisible(m_envFollow);
	m_envFollow.setBounds(86 - m_envFollow.kWidth / 2, 113 - m_envFollow.kHeight / 2, m_envFollow.kWidth, m_envFollow.kHeight);  

    _parameterBinding.bind(m_envFollow, Virus::Param_InputFollowMode);
	_parameterBinding.bind(m_envFollowAttack, Virus::Param_FilterEnvAttack);
	_parameterBinding.bind(m_eEnvFollowRelease, Virus::Param_FilterEnvDecay);
	_parameterBinding.bind(m_envFollowLevel, Virus::Param_FilterEnvSustain);

    // Distortion
	for (auto *s : {&m_distortionIntensity})
	{
		setupRotary(*this, *s);
	}
	m_distortionIntensity.setBounds(905 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_distortionCurve);
	m_distortionCurve.setBounds(741+comboBoxXMargin - comboBox3Width / 2, 113 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

    _parameterBinding.bind(m_distortionIntensity, Virus::Param_DistortionIntensity);
	_parameterBinding.bind(m_distortionCurve, Virus::Param_DistortionCurve);

    // Chorus
	for (auto *s : {&m_chorusRate, &m_chorusDepth, &m_chorusFeedback, &m_chorusMix, &m_chorusDelay})
	{
		setupRotary(*this, *s);
	}
	m_chorusRate.setBounds(1299 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);
	m_chorusDepth.setBounds(1440 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);
	m_chorusFeedback.setBounds(1584 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);
	m_chorusMix.setBounds(1722 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);
	m_chorusDelay.setBounds(1861 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_choruslfoShape);
	m_choruslfoShape.setBounds(1129+comboBoxXMargin - comboBox3Width / 2, 113 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

    _parameterBinding.bind(m_chorusRate, Virus::Param_ChorusRate);
	_parameterBinding.bind(m_chorusDepth, Virus::Param_ChorusDepth);
	_parameterBinding.bind(m_chorusFeedback, Virus::Param_ChorusFeedback);
	_parameterBinding.bind(m_chorusMix, Virus::Param_ChorusMix);
	_parameterBinding.bind(m_chorusDelay, Virus::Param_ChorusDelay);
	_parameterBinding.bind(m_choruslfoShape, Virus::Param_ChorusLfoShape);

    // AnalogBoost
	for (auto *s : {&m_analogBoostIntensity, &m_AnalogBoostTune})
	{
		setupRotary(*this, *s);
	}

	m_analogBoostIntensity.setBounds(2060 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);
	m_AnalogBoostTune.setBounds(2203 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);

    _parameterBinding.bind(m_analogBoostIntensity, Virus::Param_BassIntensity);
	_parameterBinding.bind(m_AnalogBoostTune, Virus::Param_BassTune);
	
    // Phaser
	for (auto *s : {&m_phaserMix, &m_phaserSpread, &m_phaserRate, &m_phaserDepth, &m_phaserFreq, &m_phaserFeedback})
	{
		setupRotary(*this, *s);
	}
	m_phaserMix.setBounds(283 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);
	m_phaserSpread.setBounds(420 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);
	m_phaserRate.setBounds(554 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);
	m_phaserDepth.setBounds(691 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);
	m_phaserFreq.setBounds(825 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);
	m_phaserFeedback.setBounds(961 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_stages);
	m_stages.setBounds(114+comboBoxXMargin - comboBox3Width / 2, 339 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	_parameterBinding.bind(m_phaserMix, Virus::Param_PhaserMix);
	_parameterBinding.bind(m_phaserSpread, Virus::Param_PhaserSpread);
	_parameterBinding.bind(m_phaserRate, Virus::Param_PhaserRate);
	_parameterBinding.bind(m_phaserDepth, Virus::Param_PhaserDepth);
	_parameterBinding.bind(m_phaserFreq, Virus::Param_PhaserFreq);
	_parameterBinding.bind(m_phaserFeedback, Virus::Param_PhaserFeedback);
	_parameterBinding.bind(m_stages, Virus::Param_PhaserMode);

    // Equalizer
	m_equalizerLowGgain, m_equalizerLowFreq, m_equalizerMidGain, m_equalizerMidFreq, m_equalizerMidQ, m_equalizerHighGgain, m_equalizerHighFreq;

	for (auto *s : {&m_equalizerLowGgain, &m_equalizerLowFreq, &m_equalizerMidGain, &m_equalizerMidFreq, &m_equalizerMidQ,
					&m_equalizerHighGgain, &m_equalizerHighFreq})
	{
		setupRotary(*this, *s);
	}
	m_equalizerLowGgain.setBounds(1164 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);
	m_equalizerLowFreq.setBounds(1304 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);
	m_equalizerMidGain.setBounds(1448 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);
	m_equalizerMidFreq.setBounds(1588 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);
	m_equalizerMidQ.setBounds(1737 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);
	m_equalizerHighGgain.setBounds(1882 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);
	m_equalizerHighFreq.setBounds(2021 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);
    
	_parameterBinding.bind(m_equalizerLowGgain, Virus::Param_LowEqGain);
	_parameterBinding.bind(m_equalizerLowFreq, Virus::Param_LowEqFreq);
	_parameterBinding.bind(m_equalizerMidGain, Virus::Param_MidEqGain);
	_parameterBinding.bind(m_equalizerMidFreq, Virus::Param_MidEqFreq);
	_parameterBinding.bind(m_equalizerMidQ, Virus::Param_MidEqQFactor);
	_parameterBinding.bind(m_equalizerHighGgain, Virus::Param_HighEqGain);
	_parameterBinding.bind(m_equalizerHighFreq, Virus::Param_HighEqFreq);

    // RingModInput
	for (auto *s : {&m_ringModMix})
	{
		setupRotary(*this, *s);
	}
	m_ringModMix.setBounds(2212 - knobSize / 2, 339 - knobSize / 2, knobSize, knobSize);

	_parameterBinding.bind(m_ringModMix, Virus::Param_InputRingMod);

    // Delay/Reverb
	//Show hide Reverb
	for (auto *s : {&m_delayReverbSend})
	{
		setupRotary(*this, *s);
	}
	m_delayReverbSend.setBounds(113 - knobSize / 2, 607 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_delayReverbMode);
	m_delayReverbMode.setBounds(113+comboBoxXMargin - comboBox3Width / 2, 763 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	_parameterBinding.bind(m_delayReverbSend, Virus::Param_EffectSend);
	_parameterBinding.bind(m_delayReverbMode, Virus::Param_DelayReverbMode);
	 
    auto p = m_controller.getParameter(Virus::Param_DelayReverbMode, 0);
    if (p) {
        const auto val = (int)p->getValueObject().getValueSource().getValue();
        DelayReverb();

        p->onValueChanged = nullptr;
        p->onValueChanged = [this, p]() {
            DelayReverb();
        };
    }

	//Reverb selected
	// Reverb
	for (auto *s : {&m_reverbDecayTime, &m_reverbDaming, &m_reverbColoration, &m_reverbPredelay, &m_reverbFeedback})
	{
		setupRotary(*this, *s);
	}

	m_reverbDecayTime.setBounds(297 - knobSize / 2, 854 - knobSize / 2, knobSize, knobSize);
	m_reverbDaming.setBounds(632 - knobSize / 2, 854 - knobSize / 2, knobSize, knobSize);
	m_reverbColoration.setBounds(771 - knobSize / 2, 854 - knobSize / 2, knobSize, knobSize);
	m_reverbPredelay.setBounds(909 - knobSize / 2, 854 - knobSize / 2, knobSize, knobSize);
	m_reverbFeedback.setBounds(1052 - knobSize / 2, 854 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_reverbType);
	m_reverbType.setBounds(467+comboBoxXMargin - comboBox3Width / 2, 854 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	_parameterBinding.bind(m_reverbDecayTime, Virus::Param_DelayRateReverbDecayTime);
	_parameterBinding.bind(m_reverbDaming, Virus::Param_DelayLfoShape);
	_parameterBinding.bind(m_reverbColoration, Virus::Param_DelayColor);
	_parameterBinding.bind(m_reverbPredelay, Virus::Param_DelayTime);
	_parameterBinding.bind(m_reverbFeedback, Virus::Param_DelayFeedback);
	_parameterBinding.bind(m_reverbType, Virus::Param_DelayDepthReverbRoomSize);

	// todo Need to check these parameters bindings for delay and reverb
	// Delay
	for (auto *s : {&m_delayTime, &m_delayRate, &m_delayFeedback, &m_delayColoration, &m_delayDepth})
	{
		setupRotary(*this, *s);
	}
	m_delayTime.setBounds(297 - knobSize / 2, 607 - knobSize / 2, knobSize, knobSize);
	m_delayRate.setBounds(632 - knobSize / 2, 607 - knobSize / 2, knobSize, knobSize);
	m_delayFeedback.setBounds(771 - knobSize / 2, 607 - knobSize / 2, knobSize, knobSize);
	m_delayColoration.setBounds(909 - knobSize / 2, 607 - knobSize / 2, knobSize, knobSize);
	m_delayDepth.setBounds(1052 - knobSize / 2, 607 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_delayClock);
	m_delayClock.setBounds(467+comboBoxXMargin - comboBox3Width / 2, 555 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	addAndMakeVisible(m_delayShape);
	m_delayShape.setBounds(467+comboBoxXMargin - comboBox3Width / 2, 653 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	_parameterBinding.bind(m_delayTime, Virus::Param_DelayTime);
	_parameterBinding.bind(m_delayRate, Virus::Param_DelayRateReverbDecayTime);
	_parameterBinding.bind(m_delayFeedback, Virus::Param_DelayFeedback);
	_parameterBinding.bind(m_delayColoration, Virus::Param_DelayColor);
	_parameterBinding.bind(m_delayDepth, Virus::Param_DelayDepthReverbRoomSize);
	_parameterBinding.bind(m_delayClock, Virus::Param_DelayClock);
	_parameterBinding.bind(m_delayShape, Virus::Param_DelayLfoShape);

    // Vocoder
	for (auto *s : {&m_vocoderCenterFreq, &m_vocoderModOffset, &m_vocoderModQ, &m_vocoderModSpread, &m_vocoderCarrQ,
					&m_vocoderCarrSpread, &m_vocoderSpectralBal, &m_vocoderBands, &m_vocoderAttack, &m_vocoderRelease,
					&m_vocoderSourceBal})
	{
		setupRotary(*this, *s);
	}

    m_vocoderCenterFreq.setBounds(1487 - knobSize / 2, 625 - knobSize / 2, knobSize, knobSize);
	m_vocoderModOffset.setBounds(1625 - knobSize / 2, 625 - knobSize / 2, knobSize, knobSize);
	m_vocoderModQ.setBounds(1768 - knobSize / 2, 625 - knobSize / 2, knobSize, knobSize);
	m_vocoderModSpread.setBounds(1912 - knobSize / 2, 625 - knobSize / 2, knobSize, knobSize);
	m_vocoderCarrQ.setBounds(2055 - knobSize / 2, 625 - knobSize / 2, knobSize, knobSize);
	m_vocoderCarrSpread.setBounds(2198 - knobSize / 2, 625 - knobSize / 2, knobSize, knobSize);
	
	m_vocoderSpectralBal.setBounds(1487 - knobSize / 2, 836 - knobSize / 2, knobSize, knobSize);
	m_vocoderBands.setBounds(1625 - knobSize / 2, 836 - knobSize / 2, knobSize, knobSize);
	m_vocoderAttack.setBounds(1768 - knobSize / 2, 836 - knobSize / 2, knobSize, knobSize);
	m_vocoderRelease.setBounds(1912 - knobSize / 2, 836 - knobSize / 2, knobSize, knobSize);
	m_vocoderSourceBal.setBounds(2055 - knobSize / 2, 836 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_vocoderMode);
	m_vocoderMode.setBounds(1273+comboBoxXMargin - comboBox3Width / 2, 672 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	addAndMakeVisible(m_vocoderModInput);
	m_vocoderModInput.setBounds(1273+comboBoxXMargin - comboBox3Width / 2, 787 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	m_vocoderLink.setBounds(1987 - m_vocoderLink.kWidth / 2, 526 - m_vocoderLink.kHeight / 2, m_vocoderLink.kWidth, m_vocoderLink.kHeight);
	addAndMakeVisible(m_vocoderLink);
	
	//todo Need to check these parameters bindings
	_parameterBinding.bind(m_vocoderCenterFreq, Virus::Param_FilterCutA);
	_parameterBinding.bind(m_vocoderModOffset, Virus::Param_FilterCutB);
	_parameterBinding.bind(m_vocoderModQ, Virus::Param_FilterResA);
	_parameterBinding.bind(m_vocoderModSpread, Virus::Param_FilterKeyFollowA);
	_parameterBinding.bind(m_vocoderCarrQ, Virus::Param_FilterResB);
	_parameterBinding.bind(m_vocoderCarrSpread, Virus::Param_FilterKeyFollowB);
	_parameterBinding.bind(m_vocoderSpectralBal, Virus::Param_FilterEnvSustainTime);

	_parameterBinding.bind(m_vocoderBands, Virus::Param_FilterEnvRelease);
	_parameterBinding.bind(m_vocoderAttack, Virus::Param_FilterEnvAttack);
	_parameterBinding.bind(m_vocoderRelease, Virus::Param_FilterEnvDecay);
	_parameterBinding.bind(m_vocoderSourceBal, Virus::Param_FilterBalance);

	_parameterBinding.bind(m_vocoderMode, Virus::Param_VocoderMode);
	_parameterBinding.bind(m_vocoderModInput, Virus::Param_InputSelect);
	_parameterBinding.bind(m_vocoderLink, Virus::Param_Filter2CutoffLink);

    auto p1 = m_controller.getParameter(Virus::Param_VocoderMode, 0);
    if (p1) {
        const auto val = (int)p1->getValueObject().getValueSource().getValue();
        Vocoder();

        p1->onValueChanged = nullptr;
        p1->onValueChanged = [this, p1]() {
            Vocoder();
        };
    }

}

void FxEditor::Vocoder()
{
	//Vocoder
	//m_vocoderMode
	auto p = m_controller.getParameter(Virus::Param_VocoderMode, 0);
    const auto value = (int)p->getValueObject().getValueSource().getValue();
    m_vocoderMode.setSelectedId(value + 1, juce::dontSendNotification);

	int iSelectedIndex = m_vocoderMode.getSelectedItemIndex();
	bool bVocoder = (iSelectedIndex > 0); 
	float fAlpha = (bVocoder)?1.0f:0.3f; 
	 
    m_vocoderCenterFreq.setEnabled(bVocoder);
	m_vocoderCenterFreq.setAlpha(fAlpha);
	m_vocoderModOffset.setEnabled(bVocoder);
	m_vocoderModOffset.setAlpha(fAlpha);
	m_vocoderModQ.setEnabled(bVocoder);
	m_vocoderModQ.setAlpha(fAlpha);
	m_vocoderModSpread.setEnabled(bVocoder);
	m_vocoderModSpread.setAlpha(fAlpha);
	m_vocoderCarrQ.setEnabled(bVocoder);
	m_vocoderCarrQ.setAlpha(fAlpha);
	m_vocoderCarrSpread.setEnabled(bVocoder);
	m_vocoderCarrSpread.setAlpha(fAlpha);
	m_vocoderSpectralBal.setEnabled(bVocoder);
	m_vocoderSpectralBal.setAlpha(fAlpha);
	m_vocoderBands.setEnabled(bVocoder);
	m_vocoderBands.setAlpha(fAlpha);
	m_vocoderAttack.setEnabled(bVocoder);
	m_vocoderAttack.setAlpha(fAlpha);
	m_vocoderRelease.setEnabled(bVocoder);
	m_vocoderRelease.setAlpha(fAlpha);
	m_vocoderSourceBal.setEnabled(bVocoder);
	m_vocoderSourceBal.setAlpha(fAlpha);
	//m_vocoderMode.setVisible(bVocoder);
	m_vocoderModInput.setEnabled(bVocoder);
	m_vocoderModInput.setAlpha(fAlpha);
	m_vocoderLink.setEnabled(bVocoder);
	m_vocoderLink.setAlpha(fAlpha);
}

void FxEditor::DelayReverb()
{
    //rebind();
	auto p = m_controller.getParameter(Virus::Param_DelayReverbMode, 0);
    const auto value = (int)p->getValueObject().getValueSource().getValue();
    m_delayReverbMode.setSelectedId(value + 1, juce::dontSendNotification);

	//Delay/Reverb
	int iSelectedIndex = m_delayReverbMode.getSelectedItemIndex();
    bool bReverb = (iSelectedIndex >= 2 && iSelectedIndex <= 4);
	float fReverbAlpha = (bReverb)?1.0f:0.3f; 
	bool bDelay = (iSelectedIndex ==1 || iSelectedIndex >= 5); 
	float fDelayAlpha = (bDelay)?1.0f:0.3f; 

	m_reverbDecayTime.setEnabled(bReverb);
	m_reverbDecayTime.setAlpha(fReverbAlpha);
	m_reverbDaming.setEnabled(bReverb);
	m_reverbDaming.setAlpha(fReverbAlpha);
	m_reverbColoration.setEnabled(bReverb);
	m_reverbColoration.setAlpha(fReverbAlpha);
	m_reverbPredelay.setEnabled(bReverb);
	m_reverbPredelay.setAlpha(fReverbAlpha);
	m_reverbFeedback.setEnabled(bReverb);
	m_reverbFeedback.setAlpha(fReverbAlpha);
	m_reverbType.setEnabled(bReverb);
	m_reverbType.setAlpha(fReverbAlpha);

	m_delayTime.setEnabled(bDelay);
	m_delayTime.setAlpha(fDelayAlpha);
	m_delayRate.setEnabled(bDelay);
	m_delayRate.setAlpha(fDelayAlpha);
	m_delayFeedback.setEnabled(bDelay);
	m_delayFeedback.setAlpha(fDelayAlpha);
	m_delayColoration.setEnabled(bDelay);
	m_delayColoration.setAlpha(fDelayAlpha);
	m_delayDepth.setEnabled(bDelay);
	m_delayDepth.setAlpha(fDelayAlpha);
	m_delayClock.setEnabled(bDelay);
	m_delayClock.setAlpha(fDelayAlpha);
	m_delayShape.setEnabled(bDelay);
	m_delayShape.setAlpha(fDelayAlpha);
}
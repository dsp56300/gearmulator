#pragma once

#include "../PluginProcessor.h"
#include "Virus_Buttons.h"


class VirusParameterBinding;

class FxEditor : public juce::Component
{
public:
    FxEditor(VirusParameterBinding& _parameterBinding, AudioPluginAudioProcessor &_processorRef);

private:
	void DelayReverb();
	void Vocoder();
    Virus::Controller &m_controller;

    // Env Follower
	juce::Slider m_envFollowLevel, m_envFollowAttack, m_eEnvFollowRelease;
	Buttons::Button3 m_envFollow;

    // Distortion
	juce::Slider m_distortionIntensity;
	juce::ComboBox m_distortionCurve;

    // Chorus
	juce::Slider m_chorusRate, m_chorusDepth, m_chorusFeedback, m_chorusMix, m_chorusDelay;
	juce::ComboBox m_choruslfoShape;

    // AnalogBoost
	juce::Slider m_analogBoostIntensity, m_AnalogBoostTune;

    // Phaser
	juce::Slider m_phaserMix, m_phaserSpread, m_phaserRate, m_phaserDepth, m_phaserFreq, m_phaserFeedback;
	juce::ComboBox m_stages;

    // Equalizer
	juce::Slider m_equalizerLowGgain, m_equalizerLowFreq, m_equalizerMidGain, m_equalizerMidFreq, m_equalizerMidQ,
		m_equalizerHighGgain, m_equalizerHighFreq;

	// Ring Mod
	juce::Slider m_ringModMix;
    
    //Delay/Reverb
	juce::Slider m_delayReverbSend;
	juce::ComboBox m_delayReverbMode;
	
	//Delay
	juce::Slider m_delayTime, m_delayRate, m_delayFeedback, m_delayColoration, m_delayDepth; 
	juce::ComboBox m_delayClock;
	juce::ComboBox m_delayShape;
    
	//Reverb
	juce::Slider m_reverbDecayTime, m_reverbDaming, m_reverbColoration, m_reverbPredelay, m_reverbFeedback;
	juce::ComboBox m_reverbType;

    //Vocoder
	juce::Slider m_vocoderCenterFreq, m_vocoderModOffset, m_vocoderModQ, m_vocoderModSpread, m_vocoderCarrQ,
		m_vocoderCarrSpread, m_vocoderSpectralBal, m_vocoderBands, m_vocoderAttack, m_vocoderRelease, m_vocoderSourceBal;

	Buttons::Button4 m_vocoderLink;
	juce::ComboBox m_vocoderMode;
	juce::ComboBox m_vocoderModInput;

    std::unique_ptr<juce::Drawable> m_background;
};

#pragma once

#include "../PluginProcessor.h"
#include "Virus_Buttons.h"
#include "Virus_LookAndFeel.h"

class VirusParameterBinding;

class OscEditor : public juce::Component
{
public:
    OscEditor(VirusParameterBinding& _parameterBinding);
    void resized() override;
	//Virus::BoxLookAndFeel landf;
	

private:

    // OSC1
    juce::Slider m_osc1Shape, m_osc1PulseWidth, m_osc1Semitone, m_osc1KeyFollow;
	juce::ComboBox m_osc1WaveSelect;

    // OSC2
	juce::Slider m_osc2Shape,m_osc2PulseWidth,m_osc2Semitone,m_osc2KeyFollow;
	juce::ComboBox m_osc2WaveSelect;

    juce::Slider m_osc2FmAmount, m_osc2Detune, m_osc2EnvFm, m_osc2envOSC, m_osc2PhaseInit;
	juce::ComboBox m_osc2FmMode;
        
    Buttons::Button3 m_syncOsc1Osc2;

    // OSC3
	juce::Slider m_osc3Semitone,m_osc3Detune;
	juce::ComboBox m_osc3Mode;

    // OSC Sub
	Buttons::Button2 m_subWaveform;
	juce::Slider m_noiseColor;

    // Unison
    juce::Slider m_detune, m_panSpread,m_lfoPhase;
    juce::ComboBox m_unisonVoices;

    // Punch
	juce::Slider m_Punch;

    // Mixer : juce::Component
    juce::Slider m_oscBalance,m_oscLevel,m_osc3Level, m_oscSublevel, m_noiseLevel,m_ringModLevel;  

    // Vel
	juce::Slider m_osc1ShapeVelocity,m_pulseWidthVelocity,m_ampVelocity, m_panoramaVelocity,m_osc2ShapeVelocity,m_fmAmountVelocity;

    // Filters
    juce::Slider m_cutoff1,m_res1, m_envAmount1,m_keyTrack1,m_resVel1,m_envVel1;
	juce::Slider m_cutoff2,m_res2, m_envAmount2, m_keyTrack2,m_resVel2,m_envVel2;
    juce::Slider m_filterBalance;

	Buttons::Button2 m_envPol1, m_envPol2;
	Buttons::Button1 m_link1, m_link2;
    juce::ComboBox m_filterMode1, m_filterMode2, m_filterRouting, m_saturationCurve, m_keyFollowBase;

    // Filter Envelope 
    juce::Slider m_fltAttack,m_fltDecay, m_fltSustain,m_fltTime,m_fltRelease;

    // Amp Envelope 
    juce::Slider m_ampAttack,m_ampDecay,m_ampSustain,m_ampTime,m_ampRelease;

    std::unique_ptr<juce::Drawable> m_background;
};

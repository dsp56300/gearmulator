#pragma once

#include "../PluginProcessor.h"
#include "Virus_Buttons.h"

class VirusParameterBinding;

class FxEditor : public juce::Component
{
public:
    FxEditor(VirusParameterBinding& _parameterBinding);

private:
    struct Distortion : juce::Component
    {
        Distortion(VirusParameterBinding &_parameterBinding);
        juce::Slider m_intensity;
        juce::ComboBox m_curve;
    } m_dist;

    struct AnalogBoost : juce::Component
    {
        AnalogBoost(VirusParameterBinding &_parameterBinding);
        juce::Slider m_boost;
        juce::Slider m_tune;
    } m_analogBoost;

    struct Phaser : juce::Component
    {
        Phaser(VirusParameterBinding &_parameterBinding);
        juce::Slider m_rate;
        juce::Slider m_freq;
        juce::Slider m_depth;
        juce::Slider m_feedback;
        juce::Slider m_spread;
        juce::Slider m_mix;
        juce::ComboBox m_stages;
    } m_phaser;

    struct Chorus : juce::Component
    {
        Chorus(VirusParameterBinding &_parameterBinding);
        juce::Slider m_rate;
        juce::Slider m_depth;
        juce::Slider m_feedback;
        juce::Slider m_delay;
        juce::Slider m_mix;
        juce::ComboBox m_lfoShape;
    } m_chorus;

    struct Equalizer : juce::Component
    {
        Equalizer(VirusParameterBinding &_parameterBinding);
        juce::Slider m_low_gain;
        juce::Slider m_low_freq;
        juce::Slider m_mid_gain;
        juce::Slider m_mid_freq;
        juce::Slider m_mid_q;
        juce::Slider m_high_gain;
        juce::Slider m_high_freq;
    } m_eq;

    struct EnvelopeFollower : juce::Component
    {
        EnvelopeFollower(VirusParameterBinding &_parameterBinding);
        juce::Slider m_gain;
        juce::Slider m_attack;
        juce::Slider m_release;
        juce::ComboBox m_input;
    } m_envFollow;

    struct Punch : juce::Component
    {
        Punch(VirusParameterBinding &_parameterBinding);
        juce::Slider m_amount;
    } m_punch;

    struct DelayAndReverb : juce::Component
    {
        DelayAndReverb(VirusParameterBinding &_parameterBinding);
        juce::Slider m_time;
        juce::Slider m_rate;
        juce::Slider m_depth;
        juce::Slider m_color;
        juce::Slider m_feedback;
        juce::ComboBox m_fxMode;

        struct Sync : juce::Component
        {
            Sync(VirusParameterBinding &_parameterBinding);
            juce::Slider m_mix;
            juce::ComboBox m_clock, m_lfoShape;
            juce::ComboBox m_reverbMode;
            juce::Slider m_damping;
        } m_sync;
    } m_delayReverb;

    struct Vocoder : juce::Component
    {
        Vocoder(VirusParameterBinding &_parameterBinding);
        juce::Slider m_sourceBalance;
        juce::Slider m_spectralBalance;
        juce::Slider m_bands;
        juce::Slider m_attack;
        juce::Slider m_release;
        Buttons::LinkButton m_link;
        juce::ComboBox m_mode;

        struct Carrier : juce::Component
        {
            Carrier(VirusParameterBinding &_parameterBinding);
            juce::Slider m_center_freq;
            juce::Slider m_q_factor;
            juce::Slider m_spread;
        } m_carrier;

        struct Modulator : juce::Component
        {
            Modulator(VirusParameterBinding &_parameterBinding);
            juce::Slider m_freq_offset;
            juce::Slider m_q_factor;
            juce::Slider m_spread;
            juce::ComboBox m_modInput;
        } m_modulator;
    } m_vocoder;

    std::unique_ptr<juce::Drawable> m_background;
};

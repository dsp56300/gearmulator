#pragma once

#include "../PluginProcessor.h"
#include "Virus_Buttons.h"

class VirusParameterBinding;

class ArpEditor : public juce::Component
{
public:
    ArpEditor(VirusParameterBinding& _parameterBinding);

private:
    struct VelocityAmount : juce::Component
    {
        VelocityAmount(VirusParameterBinding &_parameterBinding);
        juce::Slider m_osc1Shape;
        juce::Slider m_filter1Freq;
        juce::Slider m_filter1Res;
        juce::Slider m_pulseWidth;
        juce::Slider m_volume;
        juce::Slider m_panorama;
        juce::Slider m_osc2Shape;
        juce::Slider m_filter2Freq;
        juce::Slider m_filter2Res;
        juce::Slider m_fmAmount;
    } m_velocityAmount;

    struct Inputs : juce::Component
    {
        Inputs(VirusParameterBinding &_parameterBinding);
        juce::ComboBox m_inputMode, m_inputSelect;
    } m_inputs;

    struct Arpeggiator : juce::Component
    {
        Arpeggiator(VirusParameterBinding &_parameterBinding);
        juce::Slider m_globalTempo;
        juce::Slider m_noteLength;
        juce::Slider m_noteSwing;
        juce::ComboBox m_mode, m_pattern, m_octaveRange, m_resolution;
        Buttons::ArpHoldButton m_arpHold;
    } m_arp;

    struct SoftKnobs : juce::Component
    {
        SoftKnobs(VirusParameterBinding &_parameterBinding);
        juce::ComboBox m_funcAs[2], m_name[2];
    } m_softKnobs;

    struct PatchSettings : juce::Component
    {
        PatchSettings(VirusParameterBinding &_parameterBinding);
        juce::Slider m_patchVolume;
        juce::Slider m_panning;
        juce::Slider m_outputBalance;
        juce::Slider m_transpose;
        juce::ComboBox m_keyMode, m_secondaryOutput;
        juce::ComboBox m_bendScale, m_smoothMode, m_cat1, m_cat2;
        juce::ComboBox m_bendUp, m_bendDown;
    } m_patchSettings;

    std::unique_ptr<juce::Drawable> m_background;
};

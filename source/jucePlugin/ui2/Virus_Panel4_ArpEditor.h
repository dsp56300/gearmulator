#pragma once

#include "../PluginProcessor.h"
#include "Virus_Buttons.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "../VirusController.h"
#include "Virus_LookAndFeel.h"

class VirusParameterBinding;

class ArpEditor : public juce::Component, private juce::Timer
{
public:
    ArpEditor(VirusParameterBinding &_parameterBinding, AudioPluginAudioProcessor &_processorRef);
    ArpEditor::~ArpEditor();
    static constexpr auto kPartGroupId = 0x3FBBC;
private:
    void updateMidiInput(int index);
    void updateMidiOutput(int index);
    Virus::LookAndFeelSmallButton m_lookAndFeelSmallButton;

    //WorkingMode
    juce::ComboBox m_WorkingMode;

    //Channels
    void changePart(uint8_t _part);
    void setPlayMode(uint8_t _mode);
    void timerCallback() override;
    bool bRunning;
    Virus::Controller &m_controller;
    VirusParameterBinding &m_parameterBinding;   

    Buttons::Button3 m_partSelect[16];
    //juce::Label m_partLabels[16];
    Buttons::PresetButtonDown m_PresetPatch[16];
    juce::Label m_presetNames[16];
    Buttons::PresetButtonRight m_nextPatch[16];
    Buttons::PresetButtonLeft m_prevPatch[16];
    juce::Slider m_partVolumes[16];
    juce::Slider m_partPans[16];
 
    //juce::TextButton m_btSingleMode;
    //juce::TextButton m_btMultiSingleMode;
    //juce::TextButton m_btMultiMode;

    //MIDI Settings
    juce::AudioDeviceManager deviceManager;
    juce::ComboBox m_cmbMidiInput;
    juce::ComboBox m_cmbMidiOutput;
    int m_lastInputIndex = 0;
    int m_lastOutputIndex = 0;
    juce::PropertiesFile *m_properties;
    AudioPluginAudioProcessor &processorRef;

    //Inputs
    juce::ComboBox m_inputMode, m_inputSelect;
  
    //Arpeggiator
    juce::Slider m_globalTempo, m_noteLength, m_noteSwing;
    juce::ComboBox m_mode, m_pattern, m_octaveRange, m_resolution;
	Buttons::Button3 m_arpHold;

    //SoftKnobs
	juce::ComboBox m_softknob1, m_softknob2;

    //PatchSettings
    juce::Slider m_patchVolume, m_panning, m_outputBalance, m_transpose;
    juce::ComboBox m_keyMode, m_secondaryOutput;
    juce::ComboBox m_bendUp, m_bendDown, m_bendScale, m_smoothMode, m_cat1, m_cat2;
    
    std::unique_ptr<juce::Drawable> m_background;
};

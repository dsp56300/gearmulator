#pragma once

#include "../PluginProcessor.h"
#include "Virus_Buttons.h"


class VirusParameterBinding;

class LfoEditor : public juce::Component
{
public:
    LfoEditor(VirusParameterBinding& _parameterBinding);

private:
    //LFO 1
    juce::Slider m_lfo1Rate, m_lfo1Symmetry, m_lfo1Osc1Pitch, m_lfo1Osc2Pitch, m_lfo1Pw12, m_lfo1Reso12,
		m_lfo1KeyFollow, m_lfo1Keytrigger, m_lfo1AssignAmount, m_lfo1FilterGain;
	juce::ComboBox m_lfo1Clock, m_lfo1Shape, m_lfo1AssignDest;		
    Buttons::Button4 m_lfo1Link;
	Buttons::Button2 m_lfo1LfoMode;
	Buttons::Button3 m_lfo1EnvMode;

    //LFO 2
	juce::Slider m_lfo2Rate, m_lfo2Symmetry, m_lfo2Filter1Cutoff, m_lfo2Filter2Cutoff, m_lfo2Shape12, m_lfo2Panorama,
		m_lfo2KeyFollow, m_lfo2Keytrigger, m_lfo2AssignAmount, m_lfo2AmtFM;
	juce::ComboBox m_lfo2Clock, m_lfo2Shape, m_lfo2AssignDest;
	Buttons::Button4 m_lfo2Link;
	Buttons::Button2 m_lfo2LfoMode;
	Buttons::Button3 m_lfo2EnvMode;

    //LFO 3
	juce::Slider m_lfo3Rate, m_lfo3FadeIn, m_lfo3KeyFollow, m_lfo3AssignAmount;
	juce::ComboBox m_lfo3Clock, m_lfo3Shape, m_lfo3AssignDest;
	Buttons::Button2 m_lfo3LfoMode;

    //Matrix Slo1
	juce::Slider m_MatSlot1Amount1, m_MatSlot1Amount2, m_MatSlot1Amount3, m_MatSlot1Amount4;
	juce::ComboBox m_MatSlot1Source1, m_MatSlot1Source2, m_MatSlot1Source3, m_MatSlot1Source4;
	juce::ComboBox m_MatSlot1AssignDest1, m_MatSlot1AssignDest2, m_MatSlot1AssignDest3, m_MatSlot1AssignDest4;

    // Matrix Slo2
	juce::Slider m_MatSlot2Amount1, m_MatSlot2Amount2;
	juce::ComboBox m_MatSlot2Source12;
	juce::ComboBox m_MatSlot2AssignDest1, m_MatSlot2AssignDest2;

    // Matrix Slo3
	juce::Slider m_MatSlot3Amount1, m_MatSlot3Amount2, m_MatSlot3Amount3;
	juce::ComboBox m_MatSlot3Source123;
	juce::ComboBox m_MatSlot3AssignDest1, m_MatSlot3AssignDest2, m_MatSlot3AssignDest3;

    std::unique_ptr<juce::Drawable> m_background;
};

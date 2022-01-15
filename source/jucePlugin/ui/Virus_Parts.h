#pragma once

#include "../PluginProcessor.h"
#include "Virus_Buttons.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "../VirusController.h"
class VirusParameterBinding;

class Parts : public juce::Component, private juce::Timer
{
    public:
        Parts(VirusParameterBinding& _parameterBinding, Virus::Controller& _controller);
        ~Parts();
        static constexpr auto kPartGroupId = 0x3FBBC;
    private:
		void updatePlayModeButtons();
        void changePart(uint8_t _part);
        void setPlayMode(uint8_t _mode);
        void timerCallback() override;
        Virus::Controller &m_controller;
        VirusParameterBinding &m_parameterBinding;

        Buttons::PartSelectButton m_partSelect[16];
        juce::Label m_partLabels[16];
        juce::TextButton m_presetNames[16];
        juce::TextButton m_nextPatch[16];
        juce::TextButton m_prevPatch[16];
        juce::Slider m_partVolumes[16];
        juce::Slider m_partPans[16];
        juce::TextButton m_btSingleMode;
        juce::TextButton m_btMultiSingleMode;
        juce::TextButton m_btMultiMode;
};

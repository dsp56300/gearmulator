#include "Virus_Panel4_ArpEditor.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "../VirusParameterBinding.h"
#include "VirusEditor.h"

using namespace juce;
using namespace virusLib;

static uint8_t g_playMode = 0;

ArpEditor::ArpEditor(VirusParameterBinding &_parameterBinding, AudioPluginAudioProcessor &_processorRef): 
    processorRef(_processorRef), m_properties(_processorRef.getController().getConfig()), m_controller(_processorRef.getController()), m_parameterBinding(_parameterBinding) 
{
	setupBackground(*this, m_background, BinaryData::panel_4_png, BinaryData::panel_4_pngSize);
    setBounds(m_background->getDrawableBounds().toNearestIntEdges());

    int xPosMargin = 10;

    //Inputs
    addAndMakeVisible(m_inputMode);
    m_inputMode.setBounds(1488 + xPosMargin - comboBox3Width/2, 821 - comboBox3Height/2, comboBox3Width, comboBox3Height);
    addAndMakeVisible(m_inputSelect);
    m_inputSelect.setBounds(1488 + xPosMargin - comboBox3Width/2, 921 - comboBox3Height/2, comboBox3Width, comboBox3Height);

    _parameterBinding.bind(m_inputMode, Virus::Param_InputMode);
    _parameterBinding.bind(m_inputSelect, Virus::Param_InputSelect);

    //Arpeggiator
    constexpr auto y = 18;
    for (auto *s : {&m_globalTempo, &m_noteLength, &m_noteSwing})
        setupRotary(*this, *s);
    
    m_globalTempo.setBounds(1970 - knobSize / 2, 613 - knobSize / 2, knobSize, knobSize);
    m_noteLength.setBounds(1424 - knobSize / 2, 613 - knobSize / 2, knobSize, knobSize);
    m_noteSwing.setBounds(1574 - knobSize / 2, 613 - knobSize / 2, knobSize, knobSize);

    for (auto *c : {&m_mode, &m_pattern, &m_octaveRange, &m_resolution})
        addAndMakeVisible(c);

    m_mode.setBounds(1228 + xPosMargin - comboBox3Width/2, 558 - comboBox3Height/2, comboBox3Width, comboBox3Height);
    m_pattern.setBounds(1228 + xPosMargin - comboBox3Width/2, 656 - comboBox3Height/2, comboBox3Width, comboBox3Height);
    m_resolution.setBounds(1773 + xPosMargin - comboBox3Width/2, 656 - comboBox3Height/2, comboBox3Width, comboBox3Height);
    m_octaveRange.setBounds(1773 + xPosMargin - comboBox3Width/2, 558 - comboBox3Height/2, comboBox3Width, comboBox3Height);

    addAndMakeVisible(m_arpHold);

    m_arpHold.setBounds(2129 - m_arpHold.kWidth/ 2, 613 - m_arpHold.kHeight / 2, m_arpHold.kWidth, m_arpHold.kHeight);
    m_globalTempo.setEnabled(false);
    m_globalTempo.setAlpha(0.3f);

    _parameterBinding.bind(m_globalTempo, Virus::Param_ClockTempo, 0);
    _parameterBinding.bind(m_noteLength, Virus::Param_ArpNoteLength);
    _parameterBinding.bind(m_noteSwing, Virus::Param_ArpSwing);
    _parameterBinding.bind(m_mode, Virus::Param_ArpMode);
    _parameterBinding.bind(m_pattern, Virus::Param_ArpPatternSelect);
    _parameterBinding.bind(m_octaveRange, Virus::Param_ArpOctaveRange);
    _parameterBinding.bind(m_resolution, Virus::Param_ArpClock);
    _parameterBinding.bind(m_arpHold, Virus::Param_ArpHoldEnable);

    //SoftKnobs
    m_softknobFunc1.setBounds(1756 + xPosMargin - comboBox3Width/2, 822 - comboBox3Height/2, comboBox3Width, comboBox3Height); 
    m_softknobFunc2.setBounds(1756 + xPosMargin - comboBox3Width/2, 921 - comboBox3Height/2, comboBox3Width, comboBox3Height);
    m_softknobName1.setBounds(1983 + xPosMargin - comboBox3Width/2, 822 - comboBox3Height/2, comboBox3Width, comboBox3Height); 
    m_softknobName2.setBounds(1983 + xPosMargin - comboBox3Width/2, 921 - comboBox3Height/2, comboBox3Width, comboBox3Height);
    
    for (auto *c : {&m_softknobFunc1, &m_softknobFunc2, &m_softknobName1, &m_softknobName2})
        addAndMakeVisible(c);

    _parameterBinding.bind(m_softknobName1, Virus::Param_SoftKnob1ShortName);
    _parameterBinding.bind(m_softknobName2, Virus::Param_SoftKnob2ShortName);
    _parameterBinding.bind(m_softknobFunc1, Virus::Param_SoftKnob1Single);
    _parameterBinding.bind(m_softknobFunc2, Virus::Param_SoftKnob2Single);

    //PatchSettings
    for (auto *s : {&m_patchVolume, &m_panning, &m_outputBalance, &m_transpose})
        setupRotary(*this, *s);

    for (auto *c : {&m_patchVolume, &m_panning, &m_outputBalance, &m_transpose})
        addAndMakeVisible(c);

    m_patchVolume.setBounds(1428 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);
    m_panning.setBounds(1572 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);
    m_outputBalance.setBounds(1715 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);
    m_transpose.setBounds(1862 - knobSize / 2, 113 - knobSize / 2, knobSize, knobSize);

    for (auto *c : {&m_keyMode, &m_bendUp, &m_bendDown,  &m_bendScale, &m_smoothMode, &m_cat1, &m_cat2})
        addAndMakeVisible(c);

    m_keyMode.setBounds(1232 + xPosMargin - comboBox3Width/2, 113 - comboBox3Height/2, comboBox3Width, comboBox3Height); 
    m_smoothMode.setBounds(1232 + xPosMargin - comboBox3Width/2, 259 - comboBox3Height/2, comboBox3Width, comboBox3Height); 
    m_bendScale.setBounds(1436 + xPosMargin - comboBox3Width/2, 259 - comboBox3Height/2, comboBox3Width, comboBox3Height); 
    m_bendUp.setBounds(1641 + xPosMargin - comboBox3Width/2, 259 - comboBox3Height/2, comboBox3Width, comboBox3Height);
    m_bendDown.setBounds(1848 + xPosMargin - comboBox3Width/2, 259 - comboBox3Height/2, comboBox3Width, comboBox3Height);

    m_cat1.setBounds(1232 + xPosMargin - comboBox3Width/2, 382 - comboBox3Height/2, comboBox3Width, comboBox3Height);
    m_cat2.setBounds(1436 + xPosMargin - comboBox3Width/2, 382 - comboBox3Height/2, comboBox3Width, comboBox3Height);

    _parameterBinding.bind(m_patchVolume, Virus::Param_PatchVolume);
    _parameterBinding.bind(m_panning, Virus::Param_Panorama);
    //_parameterBinding.bind(m_outputBalance, Virus::Param_SecondOutputBalance);
    _parameterBinding.bind(m_transpose, Virus::Param_Transpose);
    _parameterBinding.bind(m_keyMode, Virus::Param_KeyMode);
    _parameterBinding.bind(m_bendUp, Virus::Param_BenderRangeUp);
    _parameterBinding.bind(m_bendDown, Virus::Param_BenderRangeDown);
    _parameterBinding.bind(m_bendScale, Virus::Param_BenderScale);
    _parameterBinding.bind(m_smoothMode, Virus::Param_ControlSmoothMode);
    _parameterBinding.bind(m_cat1, Virus::Param_Category1);
    _parameterBinding.bind(m_cat2, Virus::Param_Category2);
    _parameterBinding.bind(m_secondaryOutput, Virus::Param_PartOutputSelect);

    //Channels
    int iMarginYChannels = 118;
    int iMarginXChannels = 0;
    int iIndex = 0;

    for (auto pt = 0; pt < 16; pt++)
    {
        if (pt==8)
        {
            iIndex=0;
            iMarginXChannels=549;
        }

        //Buttons
        m_partSelect[pt].setBounds(95 - m_partSelect[pt].kWidth/2 + iMarginXChannels, 96 - m_partSelect[pt].kHeight/2 + iIndex*(iMarginYChannels), m_partSelect[pt].kWidth, m_partSelect[pt].kHeight);
        m_partSelect[pt].setRadioGroupId(kPartGroupId);
        m_partSelect[pt].setClickingTogglesState(true);
        m_partSelect[pt].onClick = [this, pt]() {this->changePart(pt);};
        addAndMakeVisible(m_partSelect[pt]);

        m_presetNames[pt].setBounds(245 - 225/2 + iMarginXChannels, 97 - 70/2 + iIndex * (iMarginYChannels), 225, 70);
        m_presetNames[pt].setText(m_controller.getCurrentPartPresetName(pt),  juce::dontSendNotification);
        m_presetNames[pt].setFont(juce::Font("Register", "Normal", 23.f));
        m_presetNames[pt].setJustificationType(Justification::left);
 
        addAndMakeVisible(m_presetNames[pt]);

        //Knobs
        for (auto *s : {&m_partVolumes[pt], &m_partPans[pt]})
        {
	        setupRotary(*this, *s);
        }    

        m_partVolumes[pt].setLookAndFeel(&m_lookAndFeelSmallButton);
        m_partVolumes[pt].setBounds(407 - knobSizeSmall / 2 + iMarginXChannels, 98 - knobSizeSmall / 2 + iIndex * (iMarginYChannels), knobSizeSmall, knobSizeSmall);
        _parameterBinding.bind(m_partVolumes[pt], Virus::Param_PartVolume, pt);
        addAndMakeVisible(m_partVolumes[pt]);
        
        m_partPans[pt].setLookAndFeel(&m_lookAndFeelSmallButton);
        m_partPans[pt].setBounds(495 - knobSizeSmall / 2 + iMarginXChannels, 98 - knobSizeSmall / 2 + iIndex * (iMarginYChannels), knobSizeSmall, knobSizeSmall);
        _parameterBinding.bind(m_partPans[pt], Virus::Param_Panorama, pt);
        addAndMakeVisible(m_partPans[pt]);

        iIndex++;
    }

    m_btWorkingMode.setBounds(1203 - m_btWorkingMode.kWidth / 2, 868 - m_btWorkingMode.kHeight / 2, m_btWorkingMode.kWidth, m_btWorkingMode.kHeight);
    addAndMakeVisible(m_btWorkingMode);
    m_btWorkingMode.onClick = [this]() 
    { 
        if (m_btWorkingMode.getToggleState()==1)
		{
            setPlayMode(virusLib::PlayMode::PlayModeSingle); 
        }
		else
		{
            setPlayMode(virusLib::PlayMode::PlayModeMulti); 
        }
        updatePlayModeButtons();
    };  

    m_partSelect[m_controller.getCurrentPart()].setToggleState(true, NotificationType::dontSendNotification);
    refreshParts();

    MidiInit();
}

ArpEditor::~ArpEditor()
{
    for (auto pt = 0; pt < 16; pt++)
    {
	    m_partVolumes[pt].setLookAndFeel(nullptr);
 	    m_partPans[pt].setLookAndFeel(nullptr);
    }
}

void ArpEditor::MidiInit() 
{
    //MIDI settings
    juce::String midiIn = m_properties->getValue("midi_input", "");
    juce::String midiOut = m_properties->getValue("midi_output", "");
    
    if (midiIn != "")
    {
        processorRef.setMidiInput(midiIn);
    }
    if (midiOut != "")
    {
        processorRef.setMidiOutput(midiOut);
    }

    m_cmbMidiInput.setBounds(2138 - 250 / 2, 81 - 47 / 2, 250, 47);
    m_cmbMidiOutput.setBounds(2138 - 250 / 2, 178 - 47 / 2, 250, 47);

    addAndMakeVisible(m_cmbMidiInput);
    addAndMakeVisible(m_cmbMidiOutput);

    m_cmbMidiInput.setTextWhenNoChoicesAvailable("No MIDI Inputs Enabled");
    auto midiInputs = juce::MidiInput::getAvailableDevices();
    juce::StringArray midiInputNames;
    midiInputNames.add(" - Midi In - ");
    auto inIndex = 0;

    for (int i = 0; i < midiInputs.size(); i++)
    {
        const auto input = midiInputs[i];
        if (processorRef.getMidiInput() != nullptr && input.identifier == processorRef.getMidiInput()->getIdentifier())
        {
            inIndex = i + 1;
        }
        midiInputNames.add(input.name);
    }

    m_cmbMidiInput.addItemList(midiInputNames, 1);
    m_cmbMidiInput.setSelectedItemIndex(inIndex, juce::dontSendNotification);
    m_cmbMidiOutput.setTextWhenNoChoicesAvailable("No MIDI Outputs Enabled");
    auto midiOutputs = juce::MidiOutput::getAvailableDevices();
    juce::StringArray midiOutputNames;
    midiOutputNames.add(" - Midi Out - ");
    auto outIndex = 0;
    
    for (int i = 0; i < midiOutputs.size(); i++)
    {
        const auto output = midiOutputs[i];
        if (processorRef.getMidiOutput() != nullptr &&
            output.identifier == processorRef.getMidiOutput()->getIdentifier())
        {
            outIndex = i + 1;
        }
        midiOutputNames.add(output.name);
    }

    m_cmbMidiOutput.addItemList(midiOutputNames, 1);
    m_cmbMidiOutput.setSelectedItemIndex(outIndex, juce::dontSendNotification);
    m_cmbMidiInput.onChange = [this]() { updateMidiInput(m_cmbMidiInput.getSelectedItemIndex()); };
    m_cmbMidiOutput.onChange = [this]() { updateMidiOutput(m_cmbMidiOutput.getSelectedItemIndex()); };
}


void ArpEditor::refreshParts() 
{
    const auto multiMode = m_controller.isMultiMode();
    for (auto pt = 0; pt < 16; pt++)
    {
        bool singlePartOrInMulti = pt == 0 || multiMode;
        float fAlpha=(multiMode)?1.0:0.3;

        if (pt>0)
        {
            m_partVolumes[pt].setEnabled(singlePartOrInMulti);
            m_partVolumes[pt].setAlpha(fAlpha);
            m_partPans[pt].setEnabled(singlePartOrInMulti);
            m_partPans[pt].setAlpha(fAlpha);
            m_partSelect[pt].setEnabled(singlePartOrInMulti);
            m_partSelect[pt].setAlpha(fAlpha);
            m_presetNames[pt].setEnabled(singlePartOrInMulti);
            m_presetNames[pt].setAlpha(fAlpha);
        }
        if (singlePartOrInMulti)
            m_presetNames[pt].setText(m_controller.getCurrentPartPresetName(pt),  juce::dontSendNotification);
        else
            m_presetNames[pt].setText("",  juce::dontSendNotification);
    }

    updatePlayModeButtons();
}

void ArpEditor::changePart(uint8_t _part)
{
    for (auto &p : m_partSelect)
    {
        p.setToggleState(false, juce::dontSendNotification);
    }
    m_partSelect[_part].setToggleState(true, juce::dontSendNotification);
    m_parameterBinding.setPart(_part);
    getParentComponent()->postCommandMessage(VirusEditor::Commands::UpdateParts);
    getParentComponent()->postCommandMessage(VirusEditor::Commands::Rebind);
}

void ArpEditor::setPlayMode(uint8_t _mode) 
{
    m_controller.getParameter(Virus::Param_PlayMode)->setValue(_mode);
    if (_mode == virusLib::PlayModeSingle && m_controller.getCurrentPart() != 0) 
    {
        changePart(0);
    }
    getParentComponent()->postCommandMessage(VirusEditor::Commands::Rebind);
}


void ArpEditor::updatePlayModeButtons()
{
    const auto modeParam = m_controller.getParameter(Virus::Param_PlayMode, 0);
    if (modeParam == nullptr) {
        return;
    }
    const auto _mode = (int)modeParam->getValue();
    if (_mode == virusLib::PlayModeSingle)
    {
        m_btWorkingMode.setToggleState(true, juce::dontSendNotification);
    }
    else if (_mode == virusLib::PlayModeMulti || _mode == virusLib::PlayModeMultiSingle)
    {
        m_btWorkingMode.setToggleState(false, juce::dontSendNotification);
    }
}

void ArpEditor::updateMidiInput(int index)
{
    auto list = juce::MidiInput::getAvailableDevices();

    if (index == 0)
    {
        m_properties->setValue("midi_input", "");
        m_properties->save();
        m_lastInputIndex = index;
        m_cmbMidiInput.setSelectedItemIndex(index, juce::dontSendNotification);
        return;
    }
    index--;
    auto newInput = list[index];

    if (!deviceManager.isMidiInputDeviceEnabled(newInput.identifier))
        deviceManager.setMidiInputDeviceEnabled(newInput.identifier, true);

    if (!processorRef.setMidiInput(newInput.identifier))
    {
        m_cmbMidiInput.setSelectedItemIndex(0, juce::dontSendNotification);
        m_lastInputIndex = 0;
        return;
    }

    m_properties->setValue("midi_input", newInput.identifier);
    m_properties->save();

    m_cmbMidiInput.setSelectedItemIndex(index + 1, juce::dontSendNotification);
    m_lastInputIndex = index;
}

void ArpEditor::updateMidiOutput(int index)
{
    auto list = juce::MidiOutput::getAvailableDevices();

    if (index == 0)
    {
        m_properties->setValue("midi_output", "");
        m_properties->save();
        m_cmbMidiOutput.setSelectedItemIndex(index, juce::dontSendNotification);
        m_lastOutputIndex = index;
        processorRef.setMidiOutput("");
        return;
    }
    index--;
    auto newOutput = list[index];
    if (!processorRef.setMidiOutput(newOutput.identifier))
    {
        m_cmbMidiOutput.setSelectedItemIndex(0, juce::dontSendNotification);
        m_lastOutputIndex = 0;
        return;
    }
    m_properties->setValue("midi_output", newOutput.identifier);
    m_properties->save();

    m_cmbMidiOutput.setSelectedItemIndex(index + 1, juce::dontSendNotification);
    m_lastOutputIndex = index;
}


#if NULL
ArpEditor::VelocityAmount::VelocityAmount(VirusParameterBinding &_parameterBinding)
{
    constexpr auto y = 19;
    for (auto *s : {&m_osc1Shape, &m_filter1Freq, &m_filter1Res, &m_pulseWidth, &m_volume, &m_panorama, &m_osc2Shape,
                    &m_filter2Freq, &m_filter2Res, &m_fmAmount})
        setupRotary(*this, *s);
    m_osc1Shape.setBounds(31, y, knobSize, knobSize);
    m_filter1Freq.setBounds(m_osc1Shape.getRight() - 7, y, knobSize, knobSize);
    m_filter1Res.setBounds(m_filter1Freq.getRight() - 8, y, knobSize, knobSize);
    m_pulseWidth.setBounds(m_filter1Res.getRight() - 7, y, knobSize, knobSize);
    m_volume.setBounds(m_pulseWidth.getRight() - 3, y, knobSize, knobSize);
    m_panorama.setBounds(m_volume.getRight() - 9, y, knobSize, knobSize);

    const auto y2 = m_osc1Shape.getBottom() + y;
    m_osc2Shape.setBounds(31, y2, knobSize, knobSize);
    m_filter2Freq.setBounds(m_osc1Shape.getRight() - 7, y2, knobSize, knobSize);
    m_filter2Res.setBounds(m_filter1Freq.getRight() - 8, y2, knobSize, knobSize);
    m_fmAmount.setBounds(m_filter1Res.getRight() - 7, y2, knobSize, knobSize);

    _parameterBinding.bind(m_osc1Shape, Virus::Param_Osc1ShapeVelocity);
    _parameterBinding.bind(m_filter1Freq, Virus::Param_Filter1EnvAmtVelocity);
    _parameterBinding.bind(m_filter1Res, Virus::Param_Resonance1Velocity);
    _parameterBinding.bind(m_pulseWidth, Virus::Param_PulseWidthVelocity);
    _parameterBinding.bind(m_volume, Virus::Param_AmpVelocity);
    _parameterBinding.bind(m_panorama, Virus::Param_PanoramaVelocity);
    _parameterBinding.bind(m_osc2Shape, Virus::Param_Osc2ShapeVelocity);
    _parameterBinding.bind(m_filter2Freq, Virus::Param_Filter2EnvAmtVelocity);
    _parameterBinding.bind(m_filter2Res, Virus::Param_Resonance2Velocity);
    _parameterBinding.bind(m_fmAmount, Virus::Param_FmAmountVelocity);
}

ArpEditor::Inputs::Inputs(VirusParameterBinding &_parameterBinding)
{
    addAndMakeVisible(m_inputMode);
    m_inputMode.setBounds(43, 38, comboBoxWidth, comboBoxHeight);
    addAndMakeVisible(m_inputSelect);
    m_inputSelect.setBounds(145, 38, comboBoxWidth, comboBoxHeight);

    _parameterBinding.bind(m_inputMode, Virus::Param_InputMode);
    _parameterBinding.bind(m_inputSelect, Virus::Param_InputSelect);
}

ArpEditor::Arpeggiator::Arpeggiator(VirusParameterBinding &_parameterBinding)
{
    constexpr auto y = 18;
    for (auto *s : {&m_globalTempo, &m_noteLength, &m_noteSwing})
        setupRotary(*this, *s);
    m_globalTempo.setBounds(341, y, knobSize, knobSize);
    m_noteLength.setBounds(m_globalTempo.getRight() - 8, y, knobSize, knobSize);
    m_noteSwing.setBounds(m_noteLength.getRight() - 7, y, knobSize, knobSize);

    for (auto *c : {&m_mode, &m_pattern, &m_octaveRange, &m_resolution})
        addAndMakeVisible(c);

    constexpr auto comboBoxWidth = 90;
    constexpr auto comboBoxHeight = 15;
    constexpr auto comboTopY = 35;

    m_mode.setBounds(35, 40, 100, 18);
    m_pattern.setBounds(114, comboTopY, comboBoxWidth, comboBoxHeight);
    m_resolution.setBounds(220, comboTopY, comboBoxWidth, comboBoxHeight);
    m_octaveRange.setBounds(m_pattern.getBounds().translated(0, comboBoxHeight + 18));

    addAndMakeVisible(m_arpHold);
    m_arpHold.setBounds(222, m_octaveRange.getY()+2, 28, 11);

    _parameterBinding.bind(m_globalTempo, Virus::Param_ClockTempo);
    _parameterBinding.bind(m_noteLength, Virus::Param_ArpNoteLength);
    _parameterBinding.bind(m_noteSwing, Virus::Param_ArpSwing);
    _parameterBinding.bind(m_mode, Virus::Param_ArpMode);
    _parameterBinding.bind(m_pattern, Virus::Param_ArpPatternSelect);
    _parameterBinding.bind(m_octaveRange, Virus::Param_ArpOctaveRange);
    _parameterBinding.bind(m_resolution, Virus::Param_ArpClock);
    _parameterBinding.bind(m_arpHold, Virus::Param_ArpHoldEnable);
}

ArpEditor::SoftKnobs::SoftKnobs(VirusParameterBinding &_parameterBinding)
{
    auto distance = 105;
    for (auto i = 0; i < 2; i++)
    {
        addAndMakeVisible(m_funcAs[i]);
        m_funcAs[i].setBounds(i == 0 ? 18 : 338, 42, comboBoxWidth, comboBoxHeight);
        addAndMakeVisible(m_name[i]);
        m_name[i].setBounds(m_funcAs[i].getX() + distance, 42, comboBoxWidth, comboBoxHeight);
    }
}

ArpEditor::PatchSettings::PatchSettings(VirusParameterBinding &_parameterBinding)
{
    constexpr auto y = 18;
    for (auto *s : {&m_patchVolume, &m_panning, &m_outputBalance, &m_transpose})
        setupRotary(*this, *s);
    m_patchVolume.setBounds(101, y, knobSize, knobSize);
    m_panning.setBounds(m_patchVolume.getRight() - 9, y, knobSize, knobSize);
    const auto y2 = m_patchVolume.getBottom() + 9;
    m_outputBalance.setBounds(m_patchVolume.getX(), y2, knobSize, knobSize);
    m_transpose.setBounds(m_panning.getX(), y2, knobSize, knobSize);

    for (auto *cb :
         {&m_keyMode, &m_secondaryOutput, &m_bendUp, &m_bendDown, &m_bendScale, &m_smoothMode, &m_cat1, &m_cat2})
        addAndMakeVisible(cb);


    constexpr auto yDist = 50;
    m_keyMode.setBounds(18, 42, comboBoxWidth, comboBoxHeight);
    m_secondaryOutput.setBounds(18, 122, comboBoxWidth, comboBoxHeight);
    constexpr auto x1 = 338;
    constexpr auto x2 = 444;
    m_bendUp.setBounds(x1, 42, comboBoxWidth, comboBoxHeight);
    m_bendScale.setBounds(x1, 42 + yDist, comboBoxWidth, comboBoxHeight);
    m_cat1.setBounds(x1, m_bendScale.getY() + yDist - 1, comboBoxWidth, comboBoxHeight);
    m_bendDown.setBounds(x2, 42, comboBoxWidth, comboBoxHeight);
    m_smoothMode.setBounds(x2, m_bendScale.getY(), comboBoxWidth, comboBoxHeight);
    m_cat2.setBounds(x2, m_cat1.getY(), comboBoxWidth, comboBoxHeight);

    _parameterBinding.bind(m_patchVolume, Virus::Param_PatchVolume);
    _parameterBinding.bind(m_panning, Virus::Param_Panorama);
    //_parameterBinding.bind(m_outputBalance, Virus::Param_SecondOutputBalance);
    _parameterBinding.bind(m_transpose, Virus::Param_Transpose);
    _parameterBinding.bind(m_keyMode, Virus::Param_KeyMode);
    //_parameterBinding.bind(m_secondaryOutput, Virus::Param_KeyMode);
    _parameterBinding.bind(m_bendUp, Virus::Param_BenderRangeUp);
    _parameterBinding.bind(m_bendDown, Virus::Param_BenderRangeDown);
    _parameterBinding.bind(m_bendScale, Virus::Param_BenderScale);
    _parameterBinding.bind(m_smoothMode, Virus::Param_ControlSmoothMode);
}
#endif
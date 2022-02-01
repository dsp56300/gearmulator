#include "Virus_Parts.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "../VirusParameterBinding.h"
#include "VirusEditor.h"
using namespace juce;

Parts::Parts(VirusParameterBinding & _parameterBinding, Virus::Controller& _controller) : m_parameterBinding(_parameterBinding), m_controller(_controller),
    m_btSingleMode("Single\nMode"), m_btMultiSingleMode("Multi\nSingle"), m_btMultiMode("Multi\nMode")
{
    setSize(338, 800);
    for (auto pt = 0; pt < 16; pt++)
    {
        m_partLabels[pt].setBounds(34, 161 + pt * (36), 24, 36);
        m_partLabels[pt].setText(juce::String(pt + 1), juce::dontSendNotification);
        m_partLabels[pt].setColour(0, juce::Colours::white);
        m_partLabels[pt].setColour(1, juce::Colour(45, 24, 24));
        m_partLabels[pt].setJustificationType(Justification::centred);
        addAndMakeVisible(m_partLabels[pt]);

        m_partSelect[pt].setBounds(35, 161 + pt*(36), 36, 36);
        m_partSelect[pt].setButtonText(juce::String(pt));
        m_partSelect[pt].setRadioGroupId(kPartGroupId);
        m_partSelect[pt].setClickingTogglesState(true);
        m_partSelect[pt].onClick = [this, pt]() {
            this->changePart(pt);
        };
        addAndMakeVisible(m_partSelect[pt]);

        m_presetNames[pt].setBounds(80, 171 + pt * (36) - 2, 136, 16 + 4);
        m_presetNames[pt].setButtonText(m_controller.getCurrentPartPresetName(pt));
        m_presetNames[pt].setColour(juce::TextButton::ColourIds::textColourOnId, juce::Colour(255,113,128));
        m_presetNames[pt].setColour(juce::TextButton::ColourIds::textColourOffId, juce::Colour(255, 113, 128));

        m_presetNames[pt].onClick = [this, pt]() {
            juce::PopupMenu selector;

            for (uint8_t b = 0; b < m_controller.getBankCount(); ++b)
            {
                const auto bank = virusLib::fromArrayIndex(b);
                auto presetNames = m_controller.getSinglePresetNames(bank);
                juce::PopupMenu p;
                for (uint8_t j = 0; j < 128; j++)
                {
                    const auto presetName = presetNames[j];
                    p.addItem(presetNames[j], [this, bank, j, pt, presetName] {
                        m_controller.setCurrentPartPreset(pt, bank, j);
                        m_presetNames[pt].setButtonText(presetName);
                        getParentComponent()->postCommandMessage(VirusEditor::Commands::UpdateParts);
                    });
                }
                std::stringstream bankName;
                bankName << "Bank " << static_cast<char>('A' + b);
                selector.addSubMenu(std::string(bankName.str()), p);
            }
            selector.showMenu(juce::PopupMenu::Options());
        };
        addAndMakeVisible(m_presetNames[pt]);

        m_prevPatch[pt].setBounds(228, 173 + 36*pt - 2, 16, 14);
        m_nextPatch[pt].setBounds(247, 173 + 36*pt - 2, 16, 14);
        m_prevPatch[pt].setButtonText("<");
        m_nextPatch[pt].setButtonText(">");
        m_prevPatch[pt].setColour(juce::TextButton::ColourIds::textColourOnId, juce::Colour(255, 113, 128));
        m_nextPatch[pt].setColour(juce::TextButton::ColourIds::textColourOnId, juce::Colour(255, 113, 128));
        m_prevPatch[pt].setColour(juce::TextButton::ColourIds::textColourOffId, juce::Colour(255, 113, 128));
        m_nextPatch[pt].setColour(juce::TextButton::ColourIds::textColourOffId, juce::Colour(255, 113, 128));
        m_prevPatch[pt].onClick = [this, pt]() {
            m_controller.setCurrentPartPreset(
                pt, m_controller.getCurrentPartBank(pt),
                std::max(0, m_controller.getCurrentPartProgram(pt) - 1));
            getParentComponent()->postCommandMessage(VirusEditor::Commands::UpdateParts);
        };
        m_nextPatch[pt].onClick = [this, pt]() {
            m_controller.setCurrentPartPreset(
                pt, m_controller.getCurrentPartBank(pt),
                std::min(127, m_controller.getCurrentPartProgram(pt) + 1));
            getParentComponent()->postCommandMessage(VirusEditor::Commands::UpdateParts);
        };
        addAndMakeVisible(m_prevPatch[pt]);
        addAndMakeVisible(m_nextPatch[pt]);

        m_partVolumes[pt].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        m_partVolumes[pt].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        m_partVolumes[pt].setBounds(m_nextPatch[pt].getBounds().translated(m_nextPatch[pt].getWidth()+8, 0));
        m_partVolumes[pt].setSize(18,18);
        m_partVolumes[pt].getProperties().set(Virus::LookAndFeel::KnobStyleProp, Virus::LookAndFeel::KnobStyle::GENERIC_MULTI);
        m_partPans[pt].setTooltip("Part Volume");
        _parameterBinding.bind(m_partVolumes[pt], Virus::Param_PartVolume, pt);
        addAndMakeVisible(m_partVolumes[pt]);

        m_partPans[pt].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        m_partPans[pt].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        m_partPans[pt].setBounds(m_partVolumes[pt].getBounds().translated(m_partVolumes[pt].getWidth()+4, 0));
        m_partPans[pt].setSize(18,18);
        m_partPans[pt].getProperties().set(Virus::LookAndFeel::KnobStyleProp, Virus::LookAndFeel::KnobStyle::GENERIC_MULTI);
        m_partPans[pt].setTooltip("Part Pan");
        _parameterBinding.bind(m_partPans[pt], Virus::Param_Panorama, pt);
        addAndMakeVisible(m_partPans[pt]);
    }
    m_partSelect[m_controller.getCurrentPart()].setToggleState(true, NotificationType::dontSendNotification);

    m_btSingleMode.setRadioGroupId(0x3cf);
    m_btMultiMode.setRadioGroupId(0x3cf);
    m_btMultiSingleMode.setRadioGroupId(0x3cf);
    addAndMakeVisible(m_btSingleMode);
    addAndMakeVisible(m_btMultiMode);
    //addAndMakeVisible(m_btMultiSingleMode);
    m_btSingleMode.setTopLeftPosition(102, 756);
    m_btSingleMode.setSize(103, 30);

    m_btSingleMode.setColour(TextButton::ColourIds::textColourOnId, juce::Colours::white);
    m_btSingleMode.setColour(TextButton::ColourIds::textColourOffId, juce::Colours::grey);
    m_btMultiMode.setColour(TextButton::ColourIds::textColourOnId, juce::Colours::white);
    m_btMultiMode.setColour(TextButton::ColourIds::textColourOffId, juce::Colours::grey);
    m_btMultiSingleMode.setColour(TextButton::ColourIds::textColourOnId, juce::Colours::white);
    m_btMultiSingleMode.setColour(TextButton::ColourIds::textColourOffId, juce::Colours::grey);
    m_btSingleMode.onClick = [this]() { setPlayMode(virusLib::PlayMode::PlayModeSingle); };
    m_btMultiSingleMode.onClick = [this]() { setPlayMode(virusLib::PlayMode::PlayModeMultiSingle); };
    m_btMultiMode.onClick = [this]() { setPlayMode(virusLib::PlayMode::PlayModeMulti); };

    //m_btMultiSingleMode.setBounds(m_btSingleMode.getBounds().translated(m_btSingleMode.getWidth()+4, 0));
    m_btMultiMode.setBounds(m_btSingleMode.getBounds().translated(m_btSingleMode.getWidth()+4, 0));
    refreshParts();
}
Parts::~Parts() {
}
void Parts::updatePlayModeButtons()
{
    const auto modeParam = m_controller.getParameter(Virus::Param_PlayMode, 0);
    if (modeParam == nullptr) {
        return;
    }
    const auto _mode = (int)modeParam->getValue();
    if (_mode == virusLib::PlayModeSingle)
    {
        m_btSingleMode.setToggleState(true, juce::dontSendNotification);
    }
    /*else if (_mode == virusLib::PlayModeMultiSingle) // disabled for now
    {
        m_btMultiSingleMode.setToggleState(true, juce::dontSendNotification);
    }*/
    else if (_mode == virusLib::PlayModeMulti || _mode == virusLib::PlayModeMultiSingle)
    {
        m_btMultiMode.setToggleState(true, juce::dontSendNotification);
    }
}
void Parts::refreshParts() {
    const auto multiMode = m_controller.isMultiMode();
    for (auto pt = 0; pt < 16; pt++)
    {
        bool singlePartOrInMulti = pt == 0 || multiMode;
        m_presetNames[pt].setVisible(singlePartOrInMulti);
        m_prevPatch[pt].setVisible(singlePartOrInMulti);
        m_nextPatch[pt].setVisible(singlePartOrInMulti);
        if (singlePartOrInMulti)
            m_presetNames[pt].setButtonText(m_controller.getCurrentPartPresetName(pt));
    }
    updatePlayModeButtons();
}
void Parts::changePart(uint8_t _part)
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

void Parts::setPlayMode(uint8_t _mode) {

    m_controller.getParameter(Virus::Param_PlayMode)->setValue(_mode);
    if (_mode == virusLib::PlayModeSingle && m_controller.getCurrentPart() != 0) {
        changePart(0);
    }
    getParentComponent()->postCommandMessage(VirusEditor::Commands::Rebind);
}

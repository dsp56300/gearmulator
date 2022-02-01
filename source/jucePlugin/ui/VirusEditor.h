#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "Virus_Buttons.h"
#include "Virus_LookAndFeel.h"
#include "../VirusController.h"

class VirusParameterBinding;
class OscEditor;
class LfoEditor;
class FxEditor;
class ArpEditor;
class PatchBrowser;
class Parts;

class VirusEditor : public juce::Component
{
public:
    VirusEditor(VirusParameterBinding &_parameterBinding, AudioPluginAudioProcessor &_processorRef);
    ~VirusEditor() override;
    void resized() override;
    void recreateControls();
    void updateParts();
    void loadFile();
    void saveFile();

    enum Commands {
        None,
        Rebind = 0x100,
        UpdateParts = 0x101,
    };
private:
    void handleCommandMessage(int commandId) override;
    void updateMidiInput(int index);
    void updateMidiOutput(int index);

    juce::Label m_version;
    juce::Label m_patchName;
    juce::Label m_controlLabel;

    juce::ComboBox m_cmbMidiInput;
    juce::ComboBox m_cmbMidiOutput;
    juce::AudioDeviceManager deviceManager;
    juce::PropertiesFile *m_properties;
    int m_lastInputIndex = 0;
    int m_lastOutputIndex = 0;

    struct MainButtons : juce::Component, juce::Value::Listener
    {
        MainButtons();
        void setupButton (int i, std::unique_ptr<juce::Drawable>&& btn, juce::DrawableButton&);
        void valueChanged(juce::Value &) override;

        std::function<void()> updateSection;
        juce::DrawableButton m_oscFilter, m_lfoMatrix, m_effects, m_arpSettings, m_patches;
        static constexpr auto kMargin = 5;
        static constexpr auto kButtonWidth = 141;
        static constexpr auto kButtonHeight = 26;
        static constexpr auto kGroupId = 0x3FBBA;
        void applyToMainButtons(std::function<void(juce::DrawableButton *)>);
    } m_mainButtons;

    struct PresetButtons : juce::Component
    {
        PresetButtons();
        Buttons::PresetButton m_save, m_load, m_presets;
    } m_presetButtons;

    struct PartButtons : juce::Component {
        PartButtons();
    };
    void applyToSections(std::function<void(juce::Component *)>);

    VirusParameterBinding& m_parameterBinding;
    AudioPluginAudioProcessor &processorRef;
    Virus::Controller& m_controller;
    std::unique_ptr<OscEditor> m_oscEditor;
    std::unique_ptr<LfoEditor> m_lfoEditor;
    std::unique_ptr<FxEditor> m_fxEditor;
    std::unique_ptr<ArpEditor> m_arpEditor;
    std::unique_ptr<PatchBrowser> m_patchBrowser;

    std::unique_ptr<Parts> m_partList;
    std::unique_ptr<juce::Drawable> m_background;

    Virus::LookAndFeel m_lookAndFeel;

    juce::String m_previousPath;
    bool m_paramDisplayLocal = false;
    void updateControlLabel(Component *eventComponent);
    void mouseEnter (const juce::MouseEvent &event) override;
    void mouseExit (const juce::MouseEvent &event) override;
    void mouseDrag (const juce::MouseEvent &event) override;
	void mouseDown (const juce::MouseEvent &event) override;
    void mouseUp (const juce::MouseEvent &event) override;
    void mouseWheelMove (const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel) override;

};

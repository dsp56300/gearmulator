#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "Virus_Buttons.h"
#include "Virus_LookAndFeel.h"
#include "../VirusController.h"
#include "Virus_LookAndFeel.h"

class VirusParameterBinding;
class OscEditor;
class LfoEditor;
class FxEditor;
class ArpEditor;
class PatchBrowser;
class AudioProcessorEditor;

class VirusEditor : public juce::Component
{
public:
    VirusEditor(VirusParameterBinding &_parameterBinding, AudioPluginAudioProcessor &_processorRef);
    ~VirusEditor() override;
    void resized() override;
    void recreateControls();
    void updatePartsPresetNames();
    void ShowMenuePatchList();
    void ShowMainMenue();
    void updateParts();

    enum Commands {
        None,
        Rebind = 0x100,
        UpdateParts = 0x101,
        InitPatches = 0x102,
        NextPatch = 0x103,
        PrevPatch = 0x104,
        SelectFirstPatch = 0x105
    };

    const int iSkinSizeWidth = 2501, iSkinSizeHeight = 1152;
    juce::AudioProcessorEditor *m_AudioPlugInEditor;

private:
    void handleCommandMessage(int commandId) override;

    juce::Label m_version;
    juce::Label m_SynthModel;
    juce::Label m_RomName;
    juce::Label m_patchName;
    juce::Label m_controlLabel;
    juce::Label m_controlLabelValue;
    juce::Label m_ToolTip;

    Buttons::ButtonMenu m_mainMenu;
	juce::PopupMenu m_mainMenuSubSkinsize;
    juce::PopupMenu selector;
	juce::PopupMenu selectorMenu;
    juce::PopupMenu SubSkinSizeSelector;

    struct MainButtons : juce::Component, juce::Value::Listener
    {
        MainButtons();
        void setupButton (int i, std::unique_ptr<juce::Drawable>&& btn, juce::DrawableButton&);
        void valueChanged(juce::Value &) override;

        std::function<void()> updateSection;
        juce::DrawableButton m_oscFilter, m_lfoMatrix, m_effects, m_arpSettings, m_patches;
        static constexpr auto kMargin = 0;
		static constexpr auto kButtonWidth = 268;
		static constexpr auto kButtonHeight = 106/2;
        static constexpr auto kGroupId = 0x3FBBA;
        void applyToMainButtons(std::function<void(juce::DrawableButton *)>);
    } m_mainButtons;

    Buttons::PresetButtonLeft m_PresetLeft;
    Buttons::PresetButtonRight m_PresetRight;
    //Buttons::PresetButtonDown m_PresetPatchList;

    struct PartButtons : juce::Component {
        PartButtons();
    };
    void applyToSections(std::function<void(juce::Component *)>);
	void AboutWindow();
    void InitPropertiesFile();

    //PropertiesFile
    juce::PropertiesFile *m_properties;

    VirusParameterBinding& m_parameterBinding;
    AudioPluginAudioProcessor &processorRef;


    Virus::Controller& m_controller;
    std::unique_ptr<OscEditor> m_oscEditor;
    std::unique_ptr<LfoEditor> m_lfoEditor;
    std::unique_ptr<FxEditor> m_fxEditor;
    std::unique_ptr<ArpEditor> m_arpEditor;
    std::unique_ptr<PatchBrowser> m_patchBrowser;
    std::unique_ptr<juce::Drawable> m_background;

    Virus::LookAndFeel m_lookAndFeel;
    Virus::LookAndFeelButtons m_landfButtons;
    Virus::CustomLAF m_landfToolTip;

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

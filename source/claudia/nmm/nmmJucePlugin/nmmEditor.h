#pragma once
#include "jucePluginEditorLib/pluginEditor.h"
#include "jucePluginEditorLib/pluginEditorState.h"
#include "nmmDevice.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneOptionsMenuProvider.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace nmmJucePlugin
{
    class Processor;
    class Editor final : public jucePluginEditorLib::Editor,public juce::StandaloneOptionsMenuProvider,private juce::Timer,private juce::ComponentListener
    {
    public:
        Editor(Processor&,const jucePluginEditorLib::Skin&);
        ~Editor() override;
        void create() override;
        void loadPatch();
        void addStandaloneOptions(juce::PopupMenu& menu) override;
        std::pair<std::string,std::string> getDemoRestrictionText() const override {return {};}
        std::unique_ptr<jucePluginEditorLib::SettingsDeviceSpecific> createDeviceSpecificSettings(const std::string&,Rml::Element*) override;
    private:
        void timerCallback() override;
        void componentParentHierarchyChanged(juce::Component& component) override;
        void componentVisibilityChanged(juce::Component& component) override;
        void note(bool down);
        void button4(bool down);
        void selectPatch(int direction);
        void setShift(bool down);
        void updatePatchSelectMode();
        Processor& m_processor;
        std::shared_ptr<PanelState> m_panel;
        std::unique_ptr<juce::FileChooser> m_chooser;
        bool m_noteDown=false;
        bool m_button4Down=false;
        bool m_shiftDown=false;
        bool m_updatingPatchSelect=false;
        std::string m_status;
        std::array<int,4> m_lastValues{{-1,-1,-1,-1}};
    };
    class EditorState final : public jucePluginEditorLib::PluginEditorState
    {
    public:
        explicit EditorState(Processor&);
        jucePluginEditorLib::Editor* createEditor(const jucePluginEditorLib::Skin&) override;
        void initContextMenu(juceRmlUi::Menu&) override;
    };
}
